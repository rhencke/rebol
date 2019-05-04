## MAIN "R3.EXE" APPLICATION CODE

This directory contains %main.c.  It is a C program that does a very minimal
amount of loading and set up, before passing control to Rebol startup code to
handle command-line processing.  Then, it can spawn an instance of the
Rebol-based interactive console.

(That same Console extension code--which is in both Rebol and C--is used by
the JavaScript/WebAssembly build.)

Because issues like Ctrl-C handling are all managed by the console code, this
code should not be running arbitrary user code which might infinite loop.  It
only kicks off the load process, and any scripts passed on the command line
are passed to the console to run (see the /PROVOKE refinement of CONSOLE).

!!! ^-- This is not entirely true, since the user configuration file and some
other code gets run.  But `--do` code and command line scripts are run with
cancellation intact.
