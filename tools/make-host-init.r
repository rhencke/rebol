REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make REBOL host initialization code"
    File: %make-host-init.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Package: "REBOL 3 Host Kit"
    Version: 1.1.1
    Needs: 2.100.100
    Purpose: {
        Build a single init-file from a collection of scripts.
        This is used during the REBOL host startup sequence.
    }
]

do %common.r
do %common-emitter.r

; This script starts running in the %make/ directory, but the %host-main.c
; file which wants to #include "tmp-host-start.inc" currently lives in the
; %os/ directory.  (That's also where host-start.r is.)
;
change-dir %../src/os

args: parse-args system/options/args
output-dir: system/options/path/prep
mkdir/deep output-dir/os

print "--- Make Host Init Code ---"

write-c-file: function [
    return: <void>
    c-file
    source "The molded representation of the rebol source"
        [text!]
][
    e: make-emitter "Host custom init code" c-file

    compressed: gzip source

    e/emit 'compressed {
        /*
         * Gzip compression of host initialization code
         * Originally $<length of source> bytes
         */
        #define REB_INIT_SIZE $<length of compressed>
        const unsigned char Reb_Init_Code[REB_INIT_SIZE] = {
            $<Binary-To-C Compressed>
        };
    }

    e/write-emitted
]


; !!! The "host protocols" are embedded in a way that is very similar to the
; extensions.  However, they do not have any native code directly associated
; with them.  The models for modules should likely be unified.
;
; But what's happening here is just that the header and contents of the
; modules are being put into blocks so that the caller can instantiate them
; at the time they choose, by reading blocks out of the HOST-PROT variable.

buf: make text! 200000
append/line buf "host-prot: ["


for-each file [
    %prot-tls.r  ; TLS Transport Layer Security (a.k.a. the "S" in HTTPS)
    %prot-http.r  ; HTTP Client Protocol (HTTPS if used with TLS)
][
    set [header: contents:] stripload/header join %../mezz/ file
    append/line buf "["
    append/line buf header
    append/line buf "]"
    append/line buf "["
    append/line buf contents
    append/line buf "]"
]

append/line buf "]"


; In order to get the whole process rolling for the r3.exe, it needs to do
; command line processing and read any encapped code out of the executable.
; It would likely want to build on ZIP files to do this, so the unzip script
; is embedded as well.
;
; !!! Unlike the %prot-http.r and %prot-tls.r, this would be a difficult
; process to abstract to extensions...since de-encapping might be important
; to booting to the point of being able to load anything at all.

for-each file [%encap.reb %unzip.reb %host-start.r] [
    print ["loading:" file]

    set [header: contents:] stripload/header file

    is-module: false  ; currently none of these three files are modules
    if is-module [
        append/line buf "import module ["
        append/line buf header
        append/line buf "]["
        append/line buf contents
        append/line buf "]"
    ] else [
        append/line buf contents
    ]
]

; `do host-code` evaluates to the HOST-START function, so it is easily found
; as the result of running the code in %host-main.c
;
append/line buf ":host-start"


write-if-changed output-dir/os/tmp-host-start.r buf  ; for inspection

write-c-file output-dir/os/tmp-host-start.inc buf  ; actual usage as C file
