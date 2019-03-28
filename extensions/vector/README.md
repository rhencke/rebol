## VECTOR! EXTENSION

This is an extension containing the R3-Alpha VECTOR! datatype code, which
has been kept compiling in Ren-C.  The key difference is that it is an
optional part of the build--so that a core interpreter can be built
without VECTOR! or its support code.

Vectors were a largely unused/untested feature of R3-Alpha, the goal of
which was to store and process raw packed integers/decimals in a native
format, in a more convenient way than using a BINARY!.

### USAGE IN FFI

See FFI test code, e.g. for calling C's qsort().  The goal is that the
vector is laid out in a way that is compatible with C's notions of the
datatypes:

* int8_t/uint8_t
* int16_t/uint16_t
* int32_t/uint32_t
* int64_/uint64t
* float
* double

To be on the safe side of strict aliasing, reading these data types out
of byte buffers (returned by rebBytes()) and into variables of this type
should be done via memcpy() and not direct access to cast pointers to
the bytes in that buffer.

### MULTI-DIMENSIONAL VECTORS / MATRIX

Some attempts were made by @giuliolunati to extend the R3-Alpha vector to
multiple dimensions.  These would be much easier to implement in Ren-C.
Contact him if interested.
