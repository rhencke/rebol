## Serial Extension

The `serial://` port was initially implemented by Carl Sassenrath to use with
some INSTEON home automation devices.  Presentation here:

https://www.youtube.com/watch?v=Axus6jF6YOQ

The source samples were integrated to make a functional cross-platform
implementation by Joshua Shireman.  The implementation was split between a
"core" portion that implemented the abstract port (picking out Rebol settings
like baud rate or parity from the port spec object), and a per-platform
"serial device".

## Status

Joshua has periodically brought the implementation up to date and demonstrated
it to work on some old serial devices from his closet. :-)   However, the
Ren-C developers do not have any such devices, and the extension has no
active users.  Circa 2019, the WebAssembly Build is higher priority (which
excises the device model of R3-Alpha entirely).  So there is not much
attention paid to testing this code.

However, it is kept compiling--in large part because of any thinking points
that the code might bring up about API usage in extensions.  However, that
means that a "sufficiently motivated individual" probably wouldn't be far from
having a working serial interface, if they wanted one!  It gives a pretty good
idea of what POSIX and Windows APIs one would use, and how to use the API
to extract settings from Rebol objects into C datatypes.
