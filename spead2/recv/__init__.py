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
 - If no numpy header is present, the following may be used in the format
   with zero copy and good efficiency:

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
 - The `u`, `i`, `c` and `b` types may also be used with other sizes, but it
   will invoke a slow conversion process and is not recommended for large
   arrays. The valid range of the `c` conversion depends on the Python
   version: for Python 2 it must be 0 to 255, for Python 3 it is interpreted
   as a Unicode code point. Using any of these will also cause the return
   value to be composed from ordinary Python lists rather than using numpy.

Two cases are treated specially:

 - A zero-dimensional array is returned as a scalar, rather than a
   zero-dimensional array object.
 - A one-dimensional array of characters (numpy dtype 'S1') is converted to a
   Python string, using ASCII encoding.

Immediate values are treated as items with heap_address_bits/8
bytes, in the order they appeared in the original packet.

Planned changes
^^^^^^^^^^^^^^^

The following will be implemented, but have not yet been (TODO):

The `u` and `i` formats with up to 64 bits and a bit-width a multiple of 8 will
be implemented. This will allow storage of immediate values.
"""

import spead2 as _spead2
from spead2._recv import *

class ItemGroup(object):
    def __init__(self):
        self.items = {}

    def update(self, heap):
        for descriptor in heap.get_descriptors():
            self.items[descriptor.id] = _spead2.Item.from_raw(descriptor, bug_compat=heap.bug_compat)
        for raw_item in heap.get_items():
            try:
                item = self.items[raw_item.id]
            except KeyError:
                # TODO: log it
                pass
            else:
                item.set_from_raw(raw_item)
