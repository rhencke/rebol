## Library Extension

The LIBRARY! datatype in Rebol represents the idea of dynamically loadable
code.  It is used by the FFI to speak to an arbitrary DLL through C types,
but also used by the extension mechanism itself to deal with specially
constructed Rebol Extension DLLs--which can provide additional natives or
ports to the system.

There's a bit of an unusual aspect to having the Library code in an extension,
since it cannot itself be loaded dynamically.

At time of writing the code is untested.  The motivation of moving it to an
extension is to take optional code that isn't a high enough priority to be
in the forefront and make it something that can be worked on independently,
and not burden the rest of the system.  (e.g. the JavaScript build cannot
make use of the DLL handling).

However, it should be easier to get a handle on now that it is factored out
more clearly into an independent testable unit--when that attention is given.
