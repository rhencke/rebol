REBOL [
    Title: "Debugger Extension"
    Name: Debugger
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        The goal of the debugger in Ren-C is to be mostly usermode code, and
        to rely on leveraging exposure of the FRAME! datatype.  One of the
        concepts involved in that is that things like mappings of integers
        to frames is all part of the userspace code's responsibility.

        Original code was in C, but is being migrated out.
    }
]


backtrace*: function [
    "Backtrace to find a specific FRAME!, or other queried property."

    return: [<opt> block! frame!]
        "Nothing if printing, if specific level a frame! else block"
    start [frame!]
        "Where to consider the trace point as starting from"
    level [blank! integer! action!]
        "Stack level to return frame for (blank to list)"
    /limit "Max number of frames (pending and active), false for no limit"
        [logic! integer!]
    /brief "Do not list depths, just function labels on one line"
][
    get-frame: not blank? :level

    if get-frame [
        any [limit brief] then [
            fail "Can't use /LIMIT or /BRIEF unless getting a list of frames"
        ]

        ; See notes on handling of breakpoint below for why 0 is accepted.
        ;
        if all [integer? level | level < 0] [
            fail ["Invalid backtrace level" level]
        ]
    ]

    max-rows: case [
        blank? limit [
            20  ; Default 20, leaves room to type on 80x25 terminal
        ]
        limit = false [
            99999  ; as many frames as possible
        ]
        integer? limit [
            if limit < 0 [
                fail ["Invalid limit of frames" frames]
            ]
            limit + 1  ; add one for ellipsis
        ]
        fail
    ]

    row: 0  ; row we're on (incl. pending frames and maybe ellipsis)
    number: 0  ; level label number in the loop (no pending frames)
    first-frame: true  ; special check of first frame for "breakpoint 0"

    f: start

    stack: collect [while [f: try parent of f] [
        if action of f = :console [
            ;
            ; For now, just skip any CONSOLE frames in the list.  It might
            ; be better to show them in some circumstances, but really they
            ; just clog up the backtrace.
            ;
            continue
        ]

        if not pending? f [
            if first-frame and [any [
                action of f = :pause
                action of f = :breakpoint
            ]][
                ; Omitting breakpoints from the list entirely presents a
                ; skewed picture of what's going on.  But giving them
                ; "index 1" means that inspecting the frame you're actually
                ; interested in (the one where you put the breakpoint) bumps
                ; to 2, which feels unnatural.
                ;
                ; Compromise by not incrementing the stack numbering for
                ; this case, leaving a leading breakpoint frame at index 0.
            ] else [
                number: me + 1
            ]
        ] else [copy []]

        first-frame: false

        row: me + 1

        ; !!! Try and keep the numbering in sync with other code that wants
        ; to be on the same page about the index.
        ;
        comment [
            assert [number = backtrace-index f]
        ]

        if get-frame [
            if integer? :level [
                if number <> :level [
                    continue
                ]
            ] else [
                assert [action? :level]
                if action of f <> :level [
                    continue
                ]
            ]
        ] else [
            if row >= max-rows [
                ;
                ; If there's more stack levels to be shown than we were asked
                ; to show, then put an `+ ...` in the list and break.
                ;
                keep '+

                if not brief [
                    ;
                    ; In the non-/ONLY backtrace, the pairing of the ellipsis
                    ; with a plus is used in order to keep the "record size"
                    ; of the list at an even 2.  Asterisk might have been
                    ; used but that is taken for "pending frames".
                    ;
                    ; !!! Review arbitrary symbolic choices.
                    ;
                    keep/line []
                    keep [*]
                ]

                break
            ]
        ]

        if get-frame [
            ;
            ; If we were fetching a single stack level, then our result will
            ; be a FRAME! (which can be queried for further properties via
            ; `near of`, `label of`, `action of`, etc.)
            ;
            return f
        ]

        ; !!! Should /BRIEF omit pending frames?  Should it have a less
        ; "loaded" name for the refinement?
        ;
        if brief [
            keep (label of f else [<anonymous>])
            continue
        ]

        keep/only near of f

        ; If building a backtrace, we just keep accumulating results as long
        ; as there are stack levels left and the limit hasn't been hit.

        ; The integer identifying the stack level (used to refer to it
        ; in other debugging commands).  Since we're going in reverse, we
        ; add it after the props so it will show up before, and give it
        ; the newline break marker.
        ;
        if pending? f [
            ;
            ; You cannot (or should not) switch to inspect a pending frame,
            ; as it is partially constructed.  It gets a "*" in the list
            ; instead of a number.
            ;
            ; !!! This may be too restrictive; though it is true you can't
            ; RESUME/FROM or UNWIND a pending frame (due to the index
            ; not knowing how many values it would have consumed if a
            ; call were to complete), inspecting the existing args could
            ; be okay.  Disallowing it offers more flexibility in the
            ; dealings with the arguments, however (for instance: not having
            ; to initialize not-yet-filled args could be one thing).
            ;
            keep [*]
        ] else [
            keep number
        ]

        keep/line []
    ]]

    ; If we ran out of stack levels before finding the single one requested
    ; via /AT, return null
    ;
    if get-frame [return null]

    ; Return accumulated backtrace otherwise, in the reverse order pushed
    ;
    return reverse stack
]


backtrace: function [
    {Prints out a backtrace at the current location}

    return: []
][
    stack: backtrace* binding of 'return _
    print mold/only stack
]


debug: function [
    {Dialect for interactive debugging, see documentation for details}
    'value [<opt> integer! frame! action! block!]
        {Stack level to inspect or dialect block, or enter debug mode}
][
    fail "Native DEBUG function hasn't been re-implemented in usermode yet"
]

sys/export [backtrace debug]
