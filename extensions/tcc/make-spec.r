REBOL [
    Title: {TCC Extension Rebmake Compiling/Linking Information}
]

name: 'TCC

source: %tcc/mod-tcc.c

includes: [
    %prep/extensions/tcc
]

; !!! It's needed to use tcc_add_symbol() on Windows to get the libRebol API
; entry points exposed.  But Linux exposes symbols automatically.  The
; process for building the librebol table is a work in progress
;
comment [
    hook: %prep-librebol-table.r

    depends: [ ; !!! Directories appear to be relative to %extensions ?
       ; %../build/prep/extensions/tcc/tmp-librebol-table.c
    ]
]


; If they installed libtcc with `sudo apt-get libtcc-dev`, then the switches
; for `-ltcc` and `#include "libtcc.h" should just work.  Otherwise, they
; have to do `export LIBTCC_INCLUDE_DIR=...` and `export LIBTCC_LIB_DIR=...`
;
; But for convenience, we try to use CONFIG_TCCDIR if they have that set *and*
; it has %libtcc.h in it.  Then it's *probably* a directory TCC was cloned
; and built in--not just where the helper library libtcc1.a was installed.

config-tccdir-with-libtcc-h: try all [
    config-tccdir: get-env "CONFIG_TCCDIR"  ; TEXT! (e.g. backslash windows)
    exists? local-to-file config-tccdir/libtcc.h
    config-tccdir
]

libtcc-include-dir: try any [
    get-env "LIBTCC_INCLUDE_DIR"
    config-tccdir-with-libtcc-h
]

libtcc-lib-dir: try any [
    get-env "LIBTCC_LIB_DIR"
    config-tccdir-with-libtcc-h
]


cflags: compose [
    ;
    ; strtold() is used by TCC in non-Windows builds.  It was a C99 addition,
    ; and Android NDKs (at least older ones) do not have it, so the link
    ; step would fail if someone didn't define it.  So this flag controls
    ; having the TCC extension define a function that just runs strtod().
    ;
    (if "1" = get-env "NEEDS_FAKE_STRTOLD" ["-DNEEDS_FAKE_STRTOLD"])

    (if libtcc-include-dir [unspaced [{-I} {"} libtcc-include-dir {"}]])
]

ldflags: compose [
    (if libtcc-lib-dir [unspaced [{-L} {"} libtcc-lib-dir {"}]])
]

libraries: [%tcc]
