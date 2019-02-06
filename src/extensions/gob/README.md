## GOB! EXTENSION

This is an extension containing the R3-Alpha GOB! datatype code, which
has been kept compiling in Ren-C.  The key difference is that it is an
optional part of the build--so that a core interpreter can be built
without GOB! or its support code.

The purpose of a GOB! was to be a more efficient data structure for
manipulating a DOM-like heirarchy of items than trying to maintain a
linked structure of Rebol values--e.g. with a parent and owner link
done by object reference, and with lists of children having to be
maintained as BLOCK!s.

In order to export the data type, it was switched to a compromise of
using arrays for the list of children, while using compact structures
for coordinates and flags.  Tradeoffs were made to keep GOB!s on
the same order of magnitude as R3-Alpha, but able to build on more
generally vetted code paths in the system.

The data type is not currently of general interest outside of the
internal implementation of the /View system.  Howver, it might aim to
become a more general DOM-NODE! sort of type, which could be useful.
