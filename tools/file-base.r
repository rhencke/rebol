REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Source File Database"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributos
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Lists of files used for creating makefiles.
    }
]

core: [
    ; (A)???
    a-constants.c
    a-globals.c
    a-lib.c

    ; (B)oot
    b-init.c

    ; (C)ore
    c-bind.c
    c-do.c
    c-context.c
    c-error.c
    c-eval.c
    c-function.c
    c-path.c
    c-port.c
    c-signal.c
    c-specialize.c
    c-value.c
    c-word.c

    ; (D)ebug
    d-crash.c
    d-dump.c
    d-eval.c
    d-gc.c
    d-print.c
    d-stack.c
    d-stats.c
    d-test.c
    d-trace.c

    ; (F)???
    f-blocks.c
    [
        f-deci.c

        ; May 2018 update to MSVC 2017 added warnings for Spectre mitigation.
        ; %f-deci.c is a lot of twiddly custom C code for implementing a fixed
        ; precision math type, that was for some reason a priority in R3-Alpha
        ; but isn't very central to Ren-C.  It is not a priority to audit
        ; it for speed, so allow it to be slow if MSVC compiles with /Qspectre
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
    ]
    [
        f-dtoa.c
        
        ; f-dtoa.c comes from a third party, and should be updated from their
        ; code if they change their policies, including Spectre mitigation:
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940

        <no-uninitialized>
        <implicit-fallthru>
    ]
    [
        f-enbase.c

        ; At time of writing there are 4 Spectre mitigations, which should
        ; be looked at and rewritten when there is time:
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
    ]
    f-extension.c
    f-int.c
    f-math.c
    f-modify.c
    f-qsort.c
    f-random.c
    f-round.c
    f-series.c
    f-stubs.c

    ; (L)exer
    l-scan.c
    l-types.c

    ; (M)emory
    m-gc.c
    [m-pools.c <no-uninitialized>]
    m-series.c
    m-stacks.c

    ; (N)atives
    n-control.c
    n-data.c
    n-do.c
    n-error.c
    n-function.c
    n-io.c
    n-loop.c
    n-math.c
    n-protect.c
    n-reduce.c
    n-sets.c
    n-strings.c
    n-system.c

    ; (S)trings
    s-cases.c
    s-crc.c
    s-find.c
    s-make.c
    s-mold.c
    s-ops.c

    ; (T)ypes
    t-binary.c
    t-bitset.c
    t-blank.c
    t-block.c
    t-char.c
    t-datatype.c
    t-date.c
    t-decimal.c
    t-function.c
    t-integer.c
    t-library.c
    t-logic.c
    t-map.c
    t-money.c
    t-object.c
    t-pair.c
    t-port.c
    t-quoted.c
    t-string.c
    t-time.c
    t-tuple.c
    t-typeset.c
    t-varargs.c
    t-word.c

    ; (U)??? (3rd-party code extractions)
    u-compress.c
    [u-md5.c <implicit-fallthru>]
    u-parse.c
    [
        u-sha1.c
        <implicit-fallthru>
        <no-hidden-local>
    ][
        u-zlib.c

        <no-make-header>
        <implicit-fallthru>
        <no-constant-conditional>

        ; Zlib is an active project so it would be worth it to check to see
        ; if minor patches for subverting Spectre mitigation would be taken.
        ;
        <msc:/wd5045> ;-- https://stackoverflow.com/q/50399940
    ]
]

; Files created by the make-boot process
;
generated: [
    tmp-boot-block.c
    tmp-type-hooks.c
]

made: [
    make-boot.r         core/tmp-boot-block.c
    make-headers.r      include/tmp-internals.h

    make-host-init.r    include/host-init.h
    make-os-ext.r       include/host-lib.h
    make-reb-lib.r      include/rebol.h
]

;
; NOTE: In the following file lists, a (+) preceding a file is indicative that
; it is to be searched for comment blocks around the function prototypes
; that indicate the function is to be gathered to be put into the host-lib.h
; exports.  (This is similar to what make-headers.r does when it runs over
; the Rebol Core sources, except for the host.)
;

main: 'host-main.c

os: [
    + host-device.c
    host-table.c
]

os-windows: [
    + windows/host-lib.c
]

os-posix: [
    posix/host-readline.c
    + posix/host-library.c
]

os-osx: [
    posix/host-readline.c
    + posix/host-library.c
]

; The Rebol open source build did not differentiate between linux and simply
; posix builds.  However Atronix R3/View uses a different `os-base` name.
; make.r requires an `os-(os-base)` entry here for each named target.
;
os-linux: [
    posix/host-readline.c
    + posix/host-library.c
]

; cloned from os-linux TODO: check'n'fix !!
os-android: [
    posix/host-readline.c
    + posix/host-library.c
]

os-emscripten: [
    posix/host-readline.c
    + posix/host-library.c
]

boot-files: [
    version.r
]

mezz-files: [
    ;-- There were some of these in the R3/View build
]

prot-files: [
    prot-tls.r
    prot-http.r
]

tools: [
    make-host-init.r
    make-host-ext.r
]
