REBOL []

name: 'Debugger
source: %debugger/mod-debugger.c
includes: copy [
    %prep/extensions/debugger ;for %tmp-extensions-debugger-init.inc
]
depends: [
]

; !!! The debugger spawns nested console sessions, with some customization.
; It might be desirable to be able to build without a specific idea of what
; the DEBUG-CONSOLE command does.  But for now it's an ADAPT of the CONSOLE
; function with the interface as defined in the console extension, and to
; make that adaptation at module initialization time the function must be
; already loaded in the sequence.
;
requires: 'Console
