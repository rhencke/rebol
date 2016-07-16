REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Read-Eval-Print-Loop (REPL)"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2016 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This implements a simple interactive command line for Ren-C, in a
        FOREVER loop driven by Rebol.  This lowers the barrier to being able
        to customize it or to add new debugger features.

        Though not implemented in C as the R3-Alpha REPL was, it still relies
        upon INPUT to receive lines.  INPUT reads lines from the "console
        port", which is C code that is linked to STDTERM on POSIX and the
        Win32 Console API on Windows.  Thus, the ability to control the cursor
        and use it to page through line history is still a "black box" at
        that layer.

        !!! Currently included in the mezzanine for build convenience, though
        really it should be packaged with the "host".
     }
]

; The last REPL to run code gets to set the REPL state.  This is not a
; reliable way of knowing if a REPL is currently running, as a REPL *could*
; be exited by Ctrl-C and then some code called internally which is non-REPL
; based, that could see the stale repl-state.
;
; (This is a generalized problem given the language not having constructors
; and destructors but wishing to maintain a global structures across an
; "exception".)
;
; However, currently the code that calls the REPL calls it in a loop without
; making any calls.  All halts will cross all the REPLs and start a new one.
;
repl-state: none

repl: function [
    {Implements a Read-Eval-Print-Loop (REPL) for Rebol}
][
    unless repl-state [
        about ;-- defined in %mezz-banner.r
    ]

    state: has [
        focus-frame: none
        focus-number: none
        last-error: none
        repl-frame: context-of 'state ;-- could use any local to get the frame
    ]

    ; We decide whether to offer debug interactivity based on whether
    ; `BACKTRACE 1` would give back FRAME! instead of NONE!...with a fallback
    ; on `BACKTRACE 0` (in case the user just typed BREAKPOINT directly).
    ;
    case [
        ; !!! Note the unusual order for /FROM on backtrace, due to its
        ; variadic implementation.  This is under review.
        ;
        state/focus-frame: backtrace/from :repl 1 [state/focus-number: 1]
        state/focus-frame: backtrace/from :repl 0 [state/focus-number: 0]
    ]

    error-handler: func [error] [
        ;
        ; An error occured during the evaluation (as opposed to the
        ; evaluation returning an error _value_).  PRINT instead of MOLD
        ; so it formats the message vs. showing the ERROR! properties.
        ;
        print error

        unless state/last-error [
            print "** Note: use WHY? for more error information"
            print/only newline
        ]

        state/last-error: error
    ]

    throw-handler: func [thrown name] [
        ;
        ; There are some throws that signal a need to exit the REPL.  This
        ; may not be an exhaustive list, but RESUME is needed to get out
        ; of a nested REPL at a breakpoint...and QUIT is used to exit.
        ;
        if (:name == :resume) or (:name == :quit) [
            throw/name :thrown :name
        ]

        print ["** No CATCH for THROW of" mold :thrown]
        if :name [
            print ["** THROW/NAME was" mold :name]
        ]
    ]

    forever [ ;-- outer LOOP (run individual DO evals)

        source: copy "" ;-- source code potentially built of multiple lines
        code: copy [] ;-- loaded block of source, or error if unloadable

        ; If a debug frame is in focus then show it in the prompt, e.g.
        ; as `if:|4|>>` to indicate stack frame 4 is being examined, and
        ; it was an `if` statement...so it will be used for binding (you
        ; can examine the condition and branch for instance)
        ;
        if state/focus-frame [
            if label-of state/focus-frame [
                print/only label-of state/focus-frame
                print/only ":"
            ]

            print/only "|"
            print/only state/focus-number
            print/only "|"
        ]

        print/only ">>"
        print/only space

        forever [ ;-- inner LOOP (gather potentially multi-line input)

            line: input
            if empty? line [
                break ;-- if empty line, DO whatever's in `code`, even ERROR!
            ]

            code: trap/with [
                ;
                ; Need to LOAD the string because `do "quit"` will quit the
                ; DO, while `do [quit]` will quit the intepreter (we want the
                ; latter interpretation).  Note that LOAD/ALL makes BLOCK!
                ; even for a single "word", e.g. [word]
                ;
                load/all append source line

            ] func [error] [
                ;
                ; If it was an error, check to see if it was the kind of
                ; error that comes from having partial input.  If so,
                ; CONTINUE and read more data until it's complete (or until
                ; an empty line signals to just report the error as-is)

                code: error

                switch error/code [
                    200 [
                        ; Often an invalid string (error isn't perfect but
                        ; could be tailored specifically, e.g. to report
                        ; a depth)
                        ;
                        print/only "{..."
                        print/only space

                        append source newline
                        continue
                    ]

                    201 [
                        ; Often a missing bracket (again, imperfect error
                        ; that could be improved.)
                        ;
                        case [
                            error/arg1 = "]" [print/only "["]
                            error/arg1 = ")" [print/only "("]
                            'default [break]
                        ]
                        print/only "..."
                        print/only space

                        append source newline
                        continue
                    ]
                ]
            ]

            break ;-- Exit gathering loop on all other errors (DO reports them)
        ]

        ; If we're focused on a debug frame, try binding into it
        ;
        if all [state/focus-frame | any-array? :code] [
            bind code state/focus-frame
        ]

        ; Set the global repl-state before running any code (see notes)
        ;
        set 'repl-state state

        ; The user code may try to "jump" out via TRAP, THROW (also how BREAK
        ; and CONTINUE) are implemented, or EXIT (how RETURN is implemented).
        ; For the moment, CATCH and TRAP are needed to cover these cases.
        ;
        ; The result is cleared out by default, so the handlers don't get
        ; mixed up (e.g. the error handler trying to CONTINUE the FOREVER
        ; but getting intercepted).  It can do nothing, an since the SET
        ; doesn't run there'll be no output.
        ;
        catch/any/with [
            trap/with [
                result: do code
            ] :error-handler
        ] :throw-handler

        if any-value? :result [
            print ["==" mold :result]
            print/only newline
        ]
    ]
]


why?: procedure [
    "Explain the last error in more detail."
    'err [<opt> word! path! error! none!]
        "Optional error value"
][
    last-error: all [repl-state | repl-state/last-error]

    case [
        void? :err [err: none]
        word? err [err: get err]
        path? err [err: get err]
    ]

    either all [
        error? err: any [:err last-error]
        err/type ; avoids lower level error types (like halt)
    ][
        ; In non-"NDEBUG" (No DEBUG) builds, if an error originated from the
        ; C sources then it will have a file and line number included of where
        ; the error was triggered.
        ;
        if all [
            file: attempt [last-error/__FILE__]
            line: attempt [last-error/__LINE__]
        ][
            print ["DEBUG BUILD INFO:"]
            print ["    __FILE__ =" file]
            print ["    __LINE__ =" line]
        ]

        say-browser
        err: lowercase ajoin [err/type #"-" err/id]
        browse join http://www.rebol.com/r3/docs/errors/ [err ".html"]
    ][
        print "No information is available."
    ]
]
