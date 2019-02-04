## IMAGE! EXTENSION

This is an extension containing the R3-Alpha IMAGE! datatype code.

That code had a number of problems.  R3-Alpha tried to unify a 2-dimensional
structure with the 1-dimensional indexing idea of a series.  This gave rise 
to various semantic ambiguities, e.g.

* What happens when you append a red pixel to a 1x1 image".  Do you get an
  error, a new column to make a 1x2 image, or a new row for a 2x1 image?

* How does the system handle IMAGE! values that have been advanced via NEXT
  or FIND to positions other than the head?

Some of these questions were discussed here:

https://github.com/rebol/rebol-issues/issues/801

There is nothing particularly remarkable about the implementation.  But Ren-C
has kept it compiling so R3-Alpha clients who use it could continue to do so.
Also it serves as an example of the needs of a complex extension-defined
datatype, so those can be taken into consideration.
