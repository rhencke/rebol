## EVENT! EXTENSION

This is an extension containing the R3-Alpha EVENT! datatype code, which has
been kept compiling in Ren-C.  Events were a semi-simple variation of common
eventing needs that are served by libraries like "libevent".

Event handling is a cross-cutting concern in the design of systems, and it
touches many parts.  Commands like WAIT and WAKE-UP have implementations in
the event extension, because waiting typically interacts with events,
dispatching pending ones.

This means that builds that don't use this particular event extension (such as
the JavaScript build) will be unlikely to interact with other R3-Alpha
extensions that involve asynchronous features.  That is mostly fine, as they
probably are better off with entirely new approaches--this code is kept
working largely for boostrap purposes.


## EFFICIENCY

One design point of the original EVENT! datatype was that an event be able
to fit into a cell (4 platform pointers) with no additional allocations:

    "Events are kept compact in order to fit into normal 128 bit
     value cells. This provides high performance for high frequency
     events and also good memory efficiency using standard series."

Ren-C avoids non-standard C tools like `#pragma`.  It also works within some
alignment constraints, that required some juggling around of the cell
structure to maintain the goal.  However, it does still fit such that one
event can be communicated via a REBVAL pointer.  See %reb-event.h for details.

But to achieve this, EVENT! cannot use the "extension type" mechanism--which
would require it to identify the cell as REB_CUSTOM and sacrifice one of its
three platform pointers to a type structure.  It is thus "special" for an
extension type, pre-reserving a REB_XXX ID which is mapped to the event type
hooks once the extension loads.
