REBOL [
    Title: "TCC Extension"
    Name: TCC

    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        The COMPILE usermode function is the front-end to the actual COMPILE*
        native, which interfaces directly with the libtcc API.  The front
        end's job is basically to do all the work that *can* be done in Rebol,
        so the amount of low-level C code is minimized.

        Among COMPILE's duties:

        * Transform any Rebol FILE! paths into TEXT! via FILE-TO-LOCAL, so
          that the paths contain the expected backslashes on Windows, etc.

        * For options that can be a list, make sure any missing options are
          turned into an empty block, and single options are turned into a
          single-element block.  This makes fewer cases for the C to handle.

        * If it's necessary to inject any automatic headers (e.g. to add
          {#include "rebol.h"} for accessing the libRebol API) then it does
          that here, instead of making that decision in the C code.

        * Extract embedded header files and library files to the local file
          system.  These allow a Rebol executable that is distributed as a
          single EXE to function as a "standalone" compiler.

          !!! ^-- Planned future feature: See README.md.

        These features could become more imaginative, considering COMPILE as
        a generic "make" service for an embedded TCC.  It could do multiple
        compilation and linking units, allow you to use include files via
        URL!, etc.  For now, it is primarily in service to user natives.
    }
]


compile: function [
    {Compiles one or more native functions at the same time, with options.}

    return: <void>
    compilables "Functions from MAKE-NATIVE, TEXT! strings of code, ..."
    /settings [block!] {
        The block supports the following dialect:
            options [text!]
            include-path [block! file! text!]
            library-path [block! file! text!]
            library [block! file! text!]
            runtime-path [file! text!]
            librebol-path [file! text!]
            debug [word! logic!]  ; !!! currently unimplemented
    }
    /inspect "Return the C source code as text, but don't compile it"
][
    ; !!! Due to module dependencies there's some problem with GET-ENV not
    ; being available in some builds (e.g. on Travis).  It gets added to
    ; lib but is somehow not available here.  This is a bug to look into.
    ;
    get-env: :lib/get-env

    if 0 = length of compilables [
        fail ["COMPILABLES must have at least one element"]
    ]

    ; !!! `settings` BLOCK! is preprocessed into a `config` OBJECT! for
    ; COMPILE*.  It's difficult to check a passed-in object for validity as
    ; well as add to it when needed:
    ; https://github.com/rebol/rebol-issues/issues/2334

    settings: default [[]]
    config: make object! [
        options: copy []  ; block! of text!s (compiler switches)
        include-path: copy []  ; block! of text!s (local directories)
        library-path: copy []  ; block! of text!s (local directories)
        library: copy []  ; block of text!s (local filenames)
        runtime-path: _  ; sets "CONFIG_TCCDIR" at runtime, text! or blank
        librebol-path: _  ; alternative to "LIBREBOL_INCLUDE_DIR"
        debug: _  ; logic!
    ]

    b: settings
    while [not tail? b] [
        var: (select config try match word! key: b/1) else [
            fail [{COMPILE/OPTIONS parameter} key {is not supported}]
        ]
        b: next b
        if tail? b [
            fail [{Missing argument to} key {in COMPILE}]
        ]

        arg: b/1
        b: next b

        if block? var [  ; at present, this always means multiple paths
            for-each item compose [(arg)] [
                switch type of item [
                    text! []
                    file! [item: file-to-local/full item]
                    fail ["Invalid item type for" key "-" type of item]
                ]

                ; If the var is supposed to be paths, should we check them
                ; here?  Doesn't work for lib short names or options, so
                ; maybe best to just let TCC deliver those errors.

                append var item
            ]
        ] else [
            switch key [
                'runtime-path 'librebol-path [
                    if var [fail [key "multiply specified"]]
                    config/(key): switch type of arg [
                        file! [arg]
                        text! [local-to-file arg]
                        fail [key "must be TEXT! or FILE!"]
                    ]
                ]
                'debug [  ; !!! Currently not supported by COMPILE*
                    if var [fail "DEBUG multiply specified"]
                    debug: switch arg [
                        #[true] 'true [true]
                        #[false] 'false [false]
                        fail "DEBUG must be LOGIC!"
                    ]
                ]
                fail  ; unreachable
            ]
        ]
    ]

    config/debug: default [false]

    ; !!! The pending concept is that there are embedded files in the TCC
    ; extension, and these files are extracted to the local filesystem in
    ; order to make them available.  This idea is being implemented, and it
    ; would mean adding to the include and lib directories.
    ;
    ; For now, if the options don't specify a `runtime-dir` use CONFIG_TCCDIR,
    ; which is a standard setting.

    config/runtime-path: default [try any [
        local-to-file try get-env "CONFIG_TCCDIR"  ; (backslashes on windows)

        ; !!! Guessing is probably a good idea in the long term, but in the
        ; short term it just creates unpredictability.  Avoid for now.
        ;
        ; match exists? %/usr/lib/x86_64-linux-gnu/tcc/
        ; match exists? %/usr/local/lib/tcc/
    ]]

    if not config/runtime-path [
        fail [
            {CONFIG_TCCDIR must be set in the environment or `runtime-path`}
            {must be provided in the /SETTINGS}
        ]
    ]

    if not exists? (config/runtime-path)/include [
        fail [
            {Runtime path} config/runtime-path {does not have an %include/}
            {directory.  It should have files like %stddef.h and %stdarg.h}
            {because TCC has its own definition of macros like va_arg(), that}
            {use internal helpers like __va_start that are *not* in GNU libc}
            {or the Microsoft C runtime.}
        ]
    ]

    ; If we are on Windows and no explicit include or lib paths were provided,
    ; try using the win32/include path.  %encap-tcc-resources.reb puts those
    ; into the resource zip, and it gives *some* Win32 entry points.
    ;
    ; https://repo.or.cz/tinycc.git/blob/HEAD:/win32/tcc-win32.txt
    ;
    ; This is a convenience to make simple examples work.  Once someone starts
    ; wanting to control the paths directly they may want to omit these hacky
    ; stubs entirely (e.g. use the actual Windows SDK files, maybe?)

    if 3 = fourth system/version [  ; e.g. Windows (32-bit or 64-bit)
        if empty? config/include-path [
            append config/include-path
                file-to-local/full (config/runtime-path)/win32/include
        ]
        if empty? config/library-path [
            append config/library-path
                file-to-local/full (config/runtime-path)/win32/library
        ]

        ; !!! For unknown reasons, on Win32 it does not seem that the API
        ; call to `tcc_set_lib_path()` sets the CONFIG_TCCDIR in such a way
        ; that TCC can find %libtcc1.a.  So adding the runtime path as a
        ; normal library directory.
        ;
        insert config/library-path file-to-local/full config/runtime-path
    ]

    ; Note: The few header files in %tcc/include/ must out-prioritize the ones
    ; in the standard distribution, e.g. %/usr/include/stdarg.h.  TCC's files
    ; must basically appear first in the `-I` include paths, which makes
    ; them *out-prioritize the system headers* (!)
    ;
    ; https://stackoverflow.com/questions/53154898/

    insert config/include-path file-to-local/full config/runtime-path/include

    ; The other complicating factor is that once emitted code has these
    ; references to TCC-specific internal routines not in libc, the
    ; tcc_relocate() command inside the extension (API link step) has to
    ; be able to find definitions for them.  They live in %libtcc1.a,
    ; which is generally located in the CONFIG_TCCDIR.

    if "1" = get-env "REBOL_TCC_EXTENSION_32BIT_ON_64BIT" [
        ;
        ; This is the verbatim list of library overrides that `-v` dumps out
        ; on 64-bit multilib Travis when compiling `int main() {}` with -m32:
        ;
        ;     gcc -v -m32 main.c -o main
        ;
        ; Otherwise, if you're trying to run a 32-bit Rebol on a 64-bit system
        ; then the link step of TCC would try to link to the 64-bit libs.
        ; This doesn't usually come up, because most people runnning a 32-bit
        ; Rebol are only doing so because they *can't* run on 64-bits.  But
        ; Travis containers are only 64-bit, so this helps test 32-bit builds.
        ;
        ; Better suggestions on how to do this are of course welcome.  :-/
        ;
        insert config/library-path [
            "/usr/lib/gcc/x86_64-linux-gnu/7/32/"
            "/usr/lib/gcc/x86_64-linux-gnu/7/../../../../lib32/"
            "/lib/../lib32/"
            "/usr/lib/../lib32/"
        ]
    ]

    ; If there are any "user natives" mentioned, tell COMPILE* to expose the
    ; internal libRebol symbols to the compiled code.
    ;
    ; !!! This can only work for in-memory compiles, e.g. TCC_OUTPUT_MEMORY.
    ; If COMPILE* allowed building an executable on disk (it should!) then
    ; you would need an actual copy of librebol.a to do this, which if it
    ; were shipped embedded would basically double the size of the Rebol
    ; executable (since that's the whole interpreter!).  A better option
    ; would be to encap the executable you already have as a copy with the
    ; natives loaded into it.

    librebol: _

    compilables: map-each item compilables [
        item: maybe if match [word! path!] :item [get item]

        switch type of :item [
            action! [
                librebol: /librebol
                :item
            ]
            text! [
                item
            ]
            file! [  ; Just an example idea... reading disk files?
                as text! read item
            ]
            default [
                fail ["COMPILABLES currently must be TEXT!/ACTION!/FILE!"]
            ]
        ]
    ]

    if librebol [
        insert compilables trim/auto mutable {
            /* TCC's override of <stddef.h> defines int64_t in a way that
             * might not be compatible with glibc's <stdint.h> (which at time
             * of writing defines it as a `__int64_t`.)  You might get:
             *
             *     /usr/include/x86_64-linux-gnu/bits/stdint-intn.h:27:
             *         error: incompatible redefinition of 'int64_t'
             *
             * Since TCC's stddef.h has what rebol.h needs in it anyway, try
             * just including that.  Otherwise try getting a newer version of
             * libtcc, a different gcc install, or just disable warnings.)
             */
            #define LIBREBOL_NO_STDINT
            #include <stddef.h>
            #define REBOL_IMPLICIT_END  /* TCC can do C99 macros, use them! */
            #include "rebol.h"
        }

        ; We want to embed and ship "rebol.h" automatically.  But as a first
        ; step, try overriding with the LIBREBOL_INCLUDE_DIR environment
        ; variable, if it wasn't explicitly passed in the options.

        config/librebol-path: default [try any [
            local-to-file try get-env "LIBREBOL_INCLUDE_DIR"

            ; Guess it is in the runtime directory (%encap-tcc-resources.reb
            ; puts it into the root of the zip file at the moment)
            ;
            config/runtime-path
        ]]

        ; We are going to test for %rebol.h in the path, so need a FILE!
        ;
        switch type of config/librebol-path [
            text! [config/librebol-path: my local-to-file]
            file! []
            blank! [
                fail [
                    {LIBREBOL_INCLUDE_DIR currently must be set either as an}
                    {environment variable or as LIBREBOL-PATH in /OPTIONS so}
                    {that the TCC extension knows where to find "rebol.h"}
                    {(e.g. in %make/prep/include)}
                ]
            ]
            default [
                fail ["Invalid LIBREBOL_INCLUDE_DIR:" config/librebol-path]
            ]
        ]

        if not exists? config/librebol-path/rebol.h [
            fail [
                {Looked for %rebol.h in} config/librebol-path {and did not}
                {find it.  Check your definition of LIBREBOL_INCLUDE_DIR}
            ]
        ]

        insert config/include-path file-to-local config/librebol-path
    ]

    ; Having paths as Rebol FILE! is useful for doing work, but the TCC calls
    ; want local paths.  Convert.
    ;
    config/runtime-path: my file-to-local/full
    config/librebol-path: <taken-into-account>  ; COMPILE* does not read

    result: compile*/(inspect)/(librebol) compilables config

    if inspect [
        print "== COMPILE/INSPECT CONFIGURATION =="
        probe config
        print "== COMPILE/INSPECT CODE =="
        print result
    ]
]


sys/export [compile]
