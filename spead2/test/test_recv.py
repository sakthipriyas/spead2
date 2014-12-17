from __future__ import division, print_function, unicode_literals
import spead2
import spead2.recv as recv
import struct
import numpy as np
from nose.tools import *

HEAP_CNT_ID =          0x01
HEAP_LENGTH_ID =       0x02
PAYLOAD_OFFSET_ID =    0x03
PAYLOAD_LENGTH_ID =    0x04
DESCRIPTOR_ID =        0x05
STREAM_CTRL_ID =       0x06

DESCRIPTOR_NAME_ID =   0x10
DESCRIPTOR_DESCRIPTION_ID = 0x11
DESCRIPTOR_SHAPE_ID =  0x12
DESCRIPTOR_FORMAT_ID = 0x13
DESCRIPTOR_ID_ID =     0x14
DESCRIPTOR_DTYPE_ID =  0x15

class Item(object):
    def __init__(self, id, value, immediate=False, offset=0):
        self.id = id
        self.value = value
        self.immediate = immediate
        self.offset = offset

    def encode(self, heap_address_bits):
        if self.immediate:
            return struct.pack('>Q', (1 << 63) | (self.id << heap_address_bits) | self.value)
        else:
            return struct.pack('>Q', (self.id << heap_address_bits) | self.offset)

class Flavour(object):
    def __init__(self, heap_address_bits, bug_compat=0):
        self.heap_address_bits = heap_address_bits
        self.bug_compat = bug_compat

    def _encode_be(self, size, value):
        """Encodes `value` as big-endian in `size` bytes"""
        assert size <= 8
        packed = struct.pack('>Q', value)
        return packed[8 - size:]

    def make_packet(self, items, payload):
        """Generate data for a packet at a low level. The value of non-immediate
        items are taken from the payload; the values in the objects are ignored.
        """
        data = []
        data.append(struct.pack('>BBBBHH', 0x53, 0x4,
            (64 - self.heap_address_bits) // 8, self.heap_address_bits // 8, 0,
            len(items)))
        for item in items:
            data.append(item.encode(self.heap_address_bits))
        data.append(bytes(payload))
        return b''.join(data)

    def make_format(self, format):
        if self.bug_compat & spead2.BUG_COMPAT_DESCRIPTOR_WIDTHS:
            field_size = 7
        else:
            field_size = (64 - self.heap_address_bits) // 8
        data = []
        for field in format:
            assert len(field[0]) == 1
            data.append(field[0].encode('ascii'))
            data.append(self._encode_be(field_size, field[1]))
        return b''.join(data)

    def make_shape(self, shape):
        if self.bug_compat & spead2.BUG_COMPAT_DESCRIPTOR_WIDTHS:
            field_size = 8
        else:
            field_size = self.heap_address_bits // 8 + 1
        if self.bug_compat & spead2.BUG_COMPAT_SHAPE_BIT_1:
            variable_marker = 2
        else:
            variable_marker = 1

        data = []
        for value in shape:
            if value == -1:
                data.append(struct.pack('>B', variable_marker))
                data.append(self._encode_be(field_size - 1, 0))
            elif value >= 0:
                data.append(self._encode_be(field_size, value))
            else:
                raise ValueError('Shape must contain non-negative values and -1')
        return b''.join(data)

    def make_packet_heap(self, heap_cnt, items):
        """Construct a single-packet heap."""
        payload_size = 0
        for item in items:
            if not item.immediate:
                payload_size += len(bytes(item.value))

        all_items = [
            Item(HEAP_CNT_ID, heap_cnt, True),
            Item(PAYLOAD_OFFSET_ID, 0, True),
            Item(PAYLOAD_LENGTH_ID, payload_size, True),
            Item(HEAP_LENGTH_ID, payload_size, True)]
        offset = 0
        payload = bytearray(payload_size)
        for item in items:
            if not item.immediate:
                value = bytes(item.value)
                all_items.append(Item(item.id, value, offset=offset))
                payload[offset : offset + len(value)] = value
                offset += len(value)
            else:
                all_items.append(item)
        return self.make_packet(all_items, payload)

    def make_plain_descriptor(self, id, name, description, format, shape):
        return Item(DESCRIPTOR_ID, self.make_packet_heap(1,
            [
                Item(DESCRIPTOR_ID_ID, id, True),
                Item(DESCRIPTOR_NAME_ID, name),
                Item(DESCRIPTOR_DESCRIPTION_ID, description),
                Item(DESCRIPTOR_FORMAT_ID, self.make_format(format)),
                Item(DESCRIPTOR_SHAPE_ID, self.make_shape(shape))
            ]))

    def make_numpy_descriptor(self, id, name, description, dtype, shape, fortran_order=False):
        header = str({
            'descr': np.lib.format.dtype_to_descr(np.dtype(dtype)),
            'fortran_order': bool(fortran_order),
            'shape': tuple(shape)
        })
        return Item(DESCRIPTOR_ID, self.make_packet_heap(1,
            [
                Item(DESCRIPTOR_ID_ID, id, True),
                Item(DESCRIPTOR_NAME_ID, name.encode('ascii')),
                Item(DESCRIPTOR_DESCRIPTION_ID, description.encode('ascii')),
                Item(DESCRIPTOR_DTYPE_ID, header.encode('ascii')),
                Item(DESCRIPTOR_SHAPE_ID, self.make_shape(shape))
            ]))

    def make_numpy_descriptor_from(self, id, name, description, array):
        if array.flags.c_contiguous:
            fortran_order = False
        elif array.flags.f_contiguous:
            fortran_order = True
        else:
            raise ValueError('Array must be C or Fortran-order contiguous')
        return self.make_numpy_descriptor(id, name, description, array.dtype, array.shape, fortran_order)

FLAVOUR = Flavour(48)

class TestDecode(object):
    """Various types of descriptors must be correctly interpreted to decode data"""
    def __init__(self):
        self.flavour = FLAVOUR

    def data_to_heaps(self, data):
        """Take some data and pass it through the receiver to obtain a set of heaps.
        The heaps must all fit within the ring buffer, otherwise some will be dropped.
        """
        stream = recv.Stream(self.flavour.bug_compat)
        receiver = recv.Receiver()
        receiver.add_buffer_reader(stream, data)
        receiver.start()
        receiver.stop()
        return list(stream)

    def data_to_items(self, data):
        """Take some data and pass it through the receiver to obtain a single heap,
        from which the items are extracted.
        """
        heaps = self.data_to_heaps(data)
        assert_equal(1, len(heaps))
        ig = recv.ItemGroup()
        ig.update(heaps[0])
        for id, item in ig.items.items():
            assert_equal(id, item.id)
        return ig.items

    def data_to_item(self, data, expected_id):
        """Take some data and pass it through the receiver to obtain a single heap,
        with a single item, which is returned.
        """
        items = self.data_to_items(data)
        assert_equal(1, len(items))
        assert_in(expected_id, items)
        return items[expected_id]

    def test_scalar_int(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'test_scalar_int', 'a scalar integer', [('i', 32)], []),
                Item(0x1234, struct.pack('>i', -123456789))
            ])
        item = self.data_to_item(packet, 0x1234)
        assert isinstance(item.value, np.int32)
        assert_equal(-123456789, item.value)

    def test_scalar_int_immediate(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'test_scalar_int', 'a scalar integer', [('u', 32)], []),
                Item(0x1234, 0x12345678 << (self.flavour.heap_address_bits - 32), True)
            ])
        item = self.data_to_item(packet, 0x1234)
        assert isinstance(item.value, np.uint32)
        assert_equal(0x12345678, item.value)

    def test_string(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'test_string', 'a byte string', [('c', 8)], [-1]),
                Item(0x1234, 'Hello world')
            ])
        item = self.data_to_item(packet, 0x1234)
        assert_equal(u'Hello world', item.value)

    def test_array(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'test_array', 'an array of floats', [('f', 32)], (3, 2)),
                Item(0x1234, struct.pack('>6f', *np.arange(1.5, 7.5)))
            ])
        item = self.data_to_item(packet, 0x1234)
        expected = np.array([[1.5, 2.5], [3.5, 4.5], [5.5, 6.5]], dtype=np.float32)
        np.testing.assert_equal(expected, item.value)

    def test_array_fields(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'test_array', 'an array of floats', [('f', 32), ('i', 8)], (3,)),
                Item(0x1234, struct.pack('>fbfbfb', 1.5, 1, 2.5, 2, 4.5, -4))
            ])
        item = self.data_to_item(packet, 0x1234)
        dtype = np.dtype('f4,i1')
        assert_equal(dtype, item.value.dtype)
        expected = np.array([(1.5, 1), (2.5, 2), (4.5, -4)], dtype=dtype)
        np.testing.assert_equal(expected, item.value)

    def test_array_numpy(self):
        expected = np.array([[1.25, 1.75], [2.25, 2.5]], dtype=np.float64)
        send = expected
        if self.flavour.bug_compat & spead2.BUG_COMPAT_SWAP_ENDIAN:
            send = expected.byteswap()
        else:
            send = expected
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_numpy_descriptor_from(
                    0x1234, 'test_array_numpy', 'an array of floats', expected),
                Item(0x1234, send.data)
            ])
        item = self.data_to_item(packet, 0x1234)
        assert_equal(expected.dtype, item.value.dtype)
        np.testing.assert_equal(expected, item.value)

    def test_array_numpy_fortran(self):
        expected = np.array([[1.25, 1.75], [2.25, 2.5]], dtype=np.float64).T
        assert_false(expected.flags.c_contiguous)
        send = expected
        if self.flavour.bug_compat & spead2.BUG_COMPAT_SWAP_ENDIAN:
            send = expected.byteswap()
        else:
            send = expected
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_numpy_descriptor_from(
                    0x1234, 'test_array_numpy_fortran', 'an array of floats', expected),
                Item(0x1234, send.data)
            ])
        item = self.data_to_item(packet, 0x1234)
        assert_equal(expected.dtype, item.value.dtype)
        np.testing.assert_equal(expected, item.value)

    def test_size_mismatch(self):
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_plain_descriptor(
                    0x1234, 'bad', 'an item with insufficient data', [('u', 32)], (5, 5)),
                Item(0x1234, b'\0' * 99)
            ])
        heaps = self.data_to_heaps(packet)
        assert_equal(1, len(heaps))
        ig = recv.ItemGroup()
        assert_raises(ValueError, ig.update, heaps[0])

    def test_numpy_object(self):
        """numpy dtypes can contain Python objects (by pointer). These can't be
        used for SPEAD.
        """
        dtype = np.dtype('f4,O')
        packet = self.flavour.make_packet_heap(1,
            [
                self.flavour.make_numpy_descriptor(
                    0x1234, 'object', 'an item with object pointers', dtype, (5,)),
                Item(0x1234, b'?' * 100)
            ])
        heaps = self.data_to_heaps(packet)
        assert_equal(1, len(heaps))
        ig = recv.ItemGroup()
        assert_raises(TypeError, ig.update, heaps[0])