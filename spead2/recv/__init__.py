"""Receive SPEAD protocol

Item format
===========
At present only a subset of the possible SPEAD format strings are accepted.
Also, the SPEAD protocol does not specify how items are to be represented in
Python. The following are accepted.

 - Any descriptor with a numpy header (will be handled by numpy). If the dtype
   contains only a single field which is non-native endian, it will be
   converted to native endian in-place. In other cases, the value retrieved
   from numpy will still be correct, but usage may be slow.
 - If no numpy header is present, the following may be used in the format:

   - u8, u16, u32, u64
   - i8, i16, i32, i64
   - f32, f64
   - b8
   - c8 (converted to dtype S1)

   This will be converted to a numpy dtype. If there are multiple fields,
   their names will be generated by numpy (`f0`, `f1`, etc). At most one
   element of the shape may indicate a variable-length field, whose length
   will be computed from the size of the item, or 0 if any other element of
   the shape is zero.

Planned changes
^^^^^^^^^^^^^^^

The following will be implemented, but have not yet been (TODO):

Two cases will be treated specially:

 - A zero-dimensional array is returned as a scalar, rather than a
   zero-dimensional array object (TODO: implement this)
 - A one-dimensional array of characters (numpy dtype 'S1') is converted to a
   Python string

Additionally, `u` and `i` formats with up to 64 bits and a bit-width a
multiple of 8 will be implemented. This will allow storage of immediate
values.

TODO: immediate values will be supported, as fixed-sizes fields with
heap_address_bits/8 bytes, in the order they appeared in the original packet.
"""

import numpy.lib.utils
import numpy as np
import spead2
from spead2._recv import *

class Descriptor(object):
    @classmethod
    def _parse_numpy_header(cls, header):
        try:
            d = np.lib.utils.safe_eval(header)
        except SyntaxError, e:
            msg = "Cannot parse descriptor: %r\nException: %r"
            raise ValueError(msg % (header, e))
        if not isinstance(d, dict):
            msg = "Descriptor is not a dictionary: %r"
            raise ValueError(msg % d)
        keys = d.keys()
        keys.sort()
        if keys != ['descr', 'fortran_order', 'shape']:
            msg = "Descriptor does not contain the correct keys: %r"
            raise ValueError(msg % (keys,))
        # Sanity-check the values.
        if not isinstance(d['shape'], tuple) or not all([isinstance(x, (int, long)) for x in d['shape']]):
            msg = "shape is not valid: %r"
            raise ValueError(msg % (d['shape'],))
        if not isinstance(d['fortran_order'], bool):
            msg = "fortran_order is not a valid bool: %r"
            raise ValueError(msg % (d['fortran_order'],))
        try:
            dtype = np.dtype(d['descr'])
        except TypeError, e:
            msg = "descr is not a valid dtype descriptor: %r"
            raise ValueError(msg % (d['descr'],))
        return d['shape'], d['fortran_order'], dtype

    @classmethod
    def _parse_format(cls, fmt):
        """Attempt to convert a SPEAD format specification to a numpy dtype.
        If there is an unsupported field, returns None.
        """
        fields = []
        for code, length in fmt:
            if ( (code in ('u', 'i') and length in (8, 16, 32, 64)) or
                (code == 'f' and length in (32, 64)) or
                (code == 'b' and length == 8) ):
                fields.append('>' + code + str(length // 8))
            elif code == 'c' and length == 8:
                fields.append('S1')
            else:
                return None
        return np.dtype(','.join(fields))

    def __init__(self, raw_descriptor, bug_compat=0):
        self.id = raw_descriptor.id
        self.name = raw_descriptor.name
        self.description = raw_descriptor.description
        self.format = raw_descriptor.format
        if raw_descriptor.numpy_header:
            self.shape, self.fortran_order, self.dtype = \
                    self._parse_numpy_header(raw_descriptor.numpy_header)
            if bug_compat & spead2.BUG_COMPAT_SWAP_ENDIAN:
                self.dtype = self.dtype.newbyteorder()
        else:
            self.shape = raw_descriptor.shape
            self.fortran_order = False
            self.dtype = self._parse_format(raw_descriptor.format)

class Item(Descriptor):
    def __init__(self, *args, **kw):
        super(Item, self).__init__(*args, **kw)
        self.value = None

    def dynamic_shape(self, max_elements):
        known = 1
        unknown_pos = -1
        for i, x in enumerate(self.shape):
            if x >= 0:
                known *= x
            elif unknown_pos >= 0:
                raise TypeError('Shape has multiple unknown dimensions')
            else:
                unknown_pos = i
        if unknown_pos == -1:
            return self.shape
        else:
            shape = list(self.shape)
            if known == 0:
                shape[unknown_pos] = 0
            else:
                shape[unknown_pos] = max_elements // known
            return shape

    def set_from_raw(self, raw_item):
        raw_value = raw_item.value
        if self.dtype is None or not isinstance(raw_value, memoryview):
            self.value = raw_value
        else:
            max_elements = raw_value.shape[0] // self.dtype.itemsize
            shape = self.dynamic_shape(max_elements)
            elements = int(np.product(shape))
            if elements > max_elements:
                raise TypeError('Item has too few elements for shape (%d < %d)' % (max_elements, elements))
            # For some reason, np.frombuffer doesn't work on memoryview, but np.array does
            array1d = np.array(raw_item.value, copy=False).view(dtype=self.dtype)[:elements]
            if self.dtype.byteorder in ('<', '>'):
                # Either < or > indicates non-native endianness. Swap it now
                # so that calculations later will be efficient
                dtype = self.dtype.newbyteorder()
                array1d = array1d.byteswap(True).view(dtype=dtype)
            order = 'F' if self.fortran_order else 'C'
            self.value = np.reshape(array1d, self.shape, order)

class ItemGroup(object):
    def __init__(self):
        self.items = {}

    def update(self, heap):
        for descriptor in heap.get_descriptors():
            self.items[descriptor.id] = Item(descriptor, bug_compat=heap.bug_compat)
        for raw_item in heap.get_items():
            try:
                item = self.items[raw_item.id]
            except KeyError:
                # TODO: log it
                pass
            else:
                item.set_from_raw(raw_item)
