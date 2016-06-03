REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Function Constructors"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

; Control structures evaluate to either void (if no branches taken) or the
; last value of any evaluated blocks.  This applies to everything from IF
; to CASE to WHILE.  The ? versions are tailored to return whether a branch
; was taken at all, and always return either TRUE or FALSE.
;
if?: specialize 'if [?: true]
unless?: specialize 'unless [?: true]
while?: specialize 'while [?: true]
case?: specialize 'case [?: true]
switch?: specialize 'switch [?: true]
trap?: specialize 'trap [?: true]
catch?: specialize 'catch [?: true]


does: func [
    {A shortcut to define a function that has no arguments or locals.}
    body [block!] {The body block of the function}
][
    func [] body
]

; The RETURN and LEAVE native specs are used to provide the prototype for
; the fake definitional returns.  But the only way you should be able to get
; at these natives is through the FUNC and PROC generators (when they hack
; out its function pointer to do implement the FUNC_FLAG_LEAVE_OR_RETURN).
; Should the native code itself somehow get called, it would error.
;
return: does [
    fail "RETURN called--but no function generator providing it in use"
]

leave: does [
    fail "LEAVE called--but no function generator providing it in use"
]

function: func [
    ; !!! Should have a unified constructor with PROCEDURE
    {Defines a function with all set-words as locals.}
    spec [block!] {Help string (opt) followed by arg words (and opt type and string)}
    body [block!] {The body block of the function}
    /with {Define or use a persistent object (self)}
    object [object! block! map!] {The object or spec}
    /extern words [block!] {These words are not local}
][
    ; Copy the spec and add /local to the end if not found (no deep copy needed)
    unless find spec: copy spec /local [append spec [
        /local ; In a block so the generated source gets the newlines
    ]]

    ; Collect all set-words in the body as words to be used as locals, and add
    ; them to the spec. Don't include the words already in the spec or object.
    insert find/tail spec /local collect-words/deep/set/ignore body either with [
        ; Make our own local object if a premade one is not provided
        unless object? object [object: make object! object]

        ; Make a full copy of the body, to allow reuse of the original
        body: copy/deep body

        bind body object  ; Bind any object words found in the body

        ; Ignore the words in the spec and those in the object. The spec needs
        ; to be copied since the object words shouldn't be added to the locals.
        ; ignore 'self too
        compose [(spec) 'self (words-of object) (:words)]
    ][
        ; Don't include the words in the spec, or any extern words.
        either extern [append copy spec words] [spec]
    ]

    func spec body
]


; To help for discoverability, there is SET-INFIX and INFIX?.  However, the
; term can be a misnomer if the function is more advanced, and using the
; "lookback" capabilities in another way.  Hence these return descriptive
; errors when people are "outside the bounds" of assurance RE:infixedness.

arity-of: function [
    value [any-word! any-path! function!]
][
    if path? :value [fail "arity-of for paths is not yet implemented."]

    unless function? :value [
        value: get value
        unless function? :value [return 0]
    ]

    if variadic? :value [
        fail "arity-of cannot give reliable answer for variadic functions"
    ]

    ; !!! Should willingness to take endability cause a similar error?
    ; Arguably the answer tells you an arity that at least it *will* accept,
    ; so it's not completely false.

    arity: 0
    for-each param reflect :value 'words [
        if refinement? :param [
            return arity
        ]
        arity: arity + 1
    ]
    arity
]

nfix?: function [
    n [integer!]
    name [string!]
    source [any-word! any-path!]
][
    case [
        not lookback? source [false]
        equal? n arity: arity-of source [true]
        n < arity [
            ; If the queried arity is lower than the arity of the function,
            ; assume it's ok...e.g. PREFIX? callers know INFIX? exists (but
            ; we don't assume INFIX? callers know PREFIX?/PUNCTUATOR? exist)
            false
        ]
        'default [
            fail [
                name "used on lookback function with arity" arity
                | "Use LOOKBACK? for generalized (tricky) testing"
            ]
        ]
    ]
]

endfix?: specialize :nfix? [n: 0 | name: "ENDFIX?"]
postfix?: specialize :nfix? [n: 1 | name: "POSTFIX?"]
infix?: specialize :nfix? [n: 2 | name: "INFIX?"]


set-nfix: function [
    n [integer!]
    name [string!]
    target [any-word! any-path!]
    value [function!]
][
    unless equal? n arity-of :value [
        fail [name "requires arity" n "functions, see SET/LOOKAHEAD"]
    ]
    set/lookback target :value
]

set-endfix: specialize :set-nfix [n: 0 | name: "SET-ENDFIX"]
set-postfix: specialize :set-nfix [n: 1 | name: "SET-POSTFIX"]
set-infix: specialize :set-nfix [n: 2 | name: "SET-INFIX"]


lambda: function [
    {Convenience variadic wrapper for FUNCTION constructors}
    args [<end> word! block!]
        {Block of argument words, or a single word (passed via LIT-WORD!)}
    :body [any-value! <...>]
        {Block that serves as the body or variadic elements for the body}
    /only
        {Use FUNC and do not run locals-gathering on the body}
][
    f: either only :func :function
    f case [
        not set? 'args [[]]
        word? args [reduce [args]]
        'default [args]
    ] case [
        block? first body [take body]
        'default [make block! body]
    ]
]


left-bar: func [
    {Expression barrier that evaluates to left side but executes right.}
    left [<opt> any-value!]
    right [<opt> any-value! <...>]
][
    while [not tail? right] [take right]
    :left
]

right-bar: func [
    <punctuates>
    {Expression barrier that evaluates to first expression on right.}
    right [<opt> any-value! <...>]
][
    also take right (while [not tail? right] [take right])
]

once-bar: func [
    <punctuates>
    {Expression barrier that's willing to only run one expression after it}
    right [<opt> any-value! <...>]
    :lookahead [<opt> any-value! <...>]
    look:
][
    also take right (
        unless any [
            tail? right
            bar? look: first lookahead
            all [
                find [word! function!] type-of :look
                punctuates? :look
            ]
        ][
            ; Can't tell if a PATH! is punctuating w/o risking execution :-(
            ; Be conservative. <punctuating> might not be the attribute
            ; sought after anyway, e.g. `1 + 2 || 3 + 4 print "Hi"` probably
            ; ought to be an error.  "barrier-like" may be the quality.
            ;
            fail [
                "|| expected punctuating expression, found" :look
            ]
        ]
    )
]


use: func [
    {Defines words local to a block.}
    vars [block! word!] {Local word(s) to the block}
    body [block!] {Block to evaluate}
][
    ; We are building a FUNC out of the body that was passed to us, and that
    ; body may have RETURN words with bindings in them already that we do
    ; not want to disturb with the definitional bindings in the new code.
    ; So that means either using MAKE FUNCTION! (which wouldn't disrupt
    ; RETURN bindings) or using the more friendly FUNC with <no-return>
    ; (they do the same thing, just FUNC is arity-2)
    ;
    ; <durable> is used so that the data for the locals will still be
    ; available if any of the words leak out and are accessed after the
    ; execution is finished.
    ;
    eval func compose [<durable> <no-return> /local (vars)] body
]

; !!! Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; The tolerance of ending with a set-word has been added to the context
; native, which avoids the need to mutate (or copy) the spec to add the none.
;
; !!! Ren-C intends to grow object into a richer construct with a spec.
;
object: :context

module: func [
    "Creates a new module."
    spec [block! object!] "The header block of the module (modified)"
    body [block!] "The body block of the module (modified)"
    /mixin "Mix in words from other modules"
    mixins [object!] "Words collected into an object"
    /local obj hidden w mod
][
    mixins: to-value :mixins

    ; !!! Is it a good idea to mess with the given spec and body bindings?
    ; This was done by MODULE but not seemingly automatically by MAKE MODULE!
    ;
    unbind/deep body

    ; Convert header block to standard header object:
    ;
    if block? :spec [
        unbind/deep spec
        spec: attempt [construct/with :spec system/standard/header]
    ]

    ; Validate the important fields of header:
    assert/type [
        spec object!
        body block!
        mixins [object! blank!]
        spec/name [word! blank!]
        spec/type [word! blank!]
        spec/version [tuple! blank!]
        spec/options [block! blank!]
    ]

    ; Module is an object during its initialization:
    obj: make object! 7 ; arbitrary starting size

    if find spec/options 'extension [
        append obj 'lib-base ; specific runtime values MUST BE FIRST
    ]

    unless spec/type [spec/type: 'module] ; in case not set earlier

    ; Collect 'export keyword exports, removing the keywords
    if find body 'export [
        unless block? select spec 'exports [
            repend spec ['exports make block! 10]
        ]

        ; Note: 'export overrides 'hidden, silently for now
        parse body [while [
            to 'export remove skip opt remove 'hidden opt
            [
                set w any-word! (
                    unless find spec/exports w: to word! w [
                        append spec/exports w
                    ]
                )
            |
                set w block! (
                    append spec/exports collect-words/ignore w spec/exports
                )
            ]
        ] to end]
    ]

    ; Collect 'hidden keyword words, removing the keywords. Ignore exports.
    hidden: _
    if find body 'hidden [
        hidden: make block! 10
        ; Note: Exports are not hidden, silently for now
        parse body [while [
            to 'hidden remove skip opt
            [
                set w any-word! (
                    unless find select spec 'exports w: to word! w [
                        append hidden w]
                )
            |
                set w block! (
                    append hidden collect-words/ignore w select spec 'exports
                )
            ]
        ] to end]
    ]

    ; Add hidden words next to the context (performance):
    if block? hidden [bind/new hidden obj]

    if block? hidden [protect/hide/words hidden]

    mod: to module! reduce [spec obj]

    ; Add exported words at top of context (performance):
    if block? select spec 'exports [bind/new spec/exports mod]

    either find spec/options 'isolate [
        ;
        ; All words of the module body are module variables:
        ;
        bind/new body mod

        ; The module keeps its own variables (not shared with system):
        ;
        if object? mixins [resolve mod mixins]

        comment [resolve mod sys] ; no longer done -Carl

        resolve mod lib
    ][
        ; Only top level defined words are module variables.
        ;
        bind/only/set body mod

        ; The module shares system exported variables:
        ;
        bind body lib

        comment [bind body sys] ; no longer done -Carl

        if object? mixins [bind body mixins]
    ]

    bind body mod ;-- redundant?
    do body

    ;print ["Module created" spec/name spec/version]
    mod
]

cause-error: func [
    "Causes an immediate error throw with the provided information."
    err-type [word!]
    err-id [word!]
    args
][
    ; Make sure it's a block:
    args: compose [(:args)]
    ; Filter out functional values:
    for-next args [
        if function? first args [
            change/only args spec-of first args
        ]
    ]
    ; Build and throw the error:
    fail make error! [
        type: err-type
        id:   err-id
        arg1: first args
        arg2: second args
        arg3: third args
    ]
]

default: func [
    "Set a word to a default value if it hasn't been set yet."
    'word [word! set-word! lit-word!] "The word (use :var for word! values)"
    value "The value" ; void not allowed on purpose
][
    unless all [set? word | not blank? get word] [set word :value] :value
]


ensure: func [
    {Pass through data that isn't VOID? or FALSE?, but FAIL otherwise}
    arg [<opt> any-value!]
    /value
        {Only check for ANY-VALUE? (FALSE and NONE ok, but not void)}
    /type
    types [block! datatype! typeset!]
        {FAIL only if not one of these types (block converts to TYPESET!)}

    ; !!! To be rewritten as a native once behavior is pinned down.
][
    unless any-value? :arg [
        unless type [fail "ENSURE did not expect value to be void"]
    ]

    unless type [
        unless any [arg value] [
            fail ["ENSURE did not expect arg to be" (mold :arg)]
        ]
        return :arg
    ]

    unless find (case [
        block? :types [make typeset! types]
        typeset? :types [types]
        datatype? :types [reduce [types]] ;-- we'll find DATATYPE! in a block
        fail 'unreachable
    ]) type-of :arg [
        fail ["ENSURE did not expect arg to have type" (type-of :arg)]
    ]
    :arg
]


secure: func ['d] [boot-print "SECURE is disabled"]
