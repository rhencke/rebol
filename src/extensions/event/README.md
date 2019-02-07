## EVENT! EXTENSION

This is an extension containing the R3-Alpha EVENT! datatype code,
which has been kept compiling in Ren-C.  The key difference is that
it is an optional part of the build--so that a core interpreter can
be built without GOB! or its support code.

Events were a rather simple variation of common eventing needs that
are served by libraries like "libevent".  Original comment said:

    "Events are kept compact in order to fit into normal 128 bit
     value cells. This provides high performance for high frequency
     events and also good memory efficiency using standard series."

Ren-C has a greater number of tricks for efficiency that R3-Alpha,
and can offer a spectrum of event sizes, not just single cells but
also a cell plus a single data-bearing REBSER node.
