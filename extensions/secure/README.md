## "SECURE" EXTENSION

This extension contains the unfinished R3-Alpha code to establish permission
levels used by PORT!s to check for access privileges on various directories,
network locations, CALL-ing out to other tools, debugging, etc.

http://www.rebol.com/r3/docs/functions/secure.html

It was intended to be an extension of Rebol2's feature by which you could
instruct the interpreter to either accept various file read/write operations,
reject them, or prompt each time an operation was attempted:

http://www.rebol.com/docs/setup.html#section-16

Due to Rebol not being a particularly security-oriented language, it was not
necessarily a "hardened" form of security against rogue scripts.  However,
it could be good enough to stop casual accidents from overwriting files.

## OVERLAP WITH OTHER TOOLS

Increasingly, operating systems and utilities provide protections for the
implementation of such features.  Utilities like "Little Snitch" or ZoneAlarm
can intercept network access--for instance--and inform users when a program
is trying to make outgoing connections.  There are also various forms of
sandboxing, virtual machines, and "jails" that limit permissions for specific
programs.

Hence trying to implement this at the language level may be redundant with
those efforts.  The Ren-C initiative did not prioritize work on this feature,
but also did not delete the supporting code.  Moving it to an extension is
a compromise for looking at how executables might be built with or without
the SECURE dialect.
