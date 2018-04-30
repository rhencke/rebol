; COMMENT is fully invisible.
;
; https://trello.com/c/dWQnsspG

(
    1 = do [comment "a" 1]
)
(
    1 = do [1 comment "a"]
)
(
    () = do [comment "a"]
)

(
    pos: _
    val: do/next [1 + comment "a" comment "b" 2 * 3 fail "didn't stop"] 'pos
    did all [
        val = 9
        pos = [fail "didn't stop"]
    ]
)
(
    pos: _
    val: do/next [1 comment "a" + comment "b" 2 * 3 fail "didn't stop"] 'pos
    did all [
        val = 9
        pos = [fail "didn't stop"]
    ]
)
(
    pos: _
    val: do/next [1 comment "a" comment "b" + 2 * 3 fail "didn't stop"] 'pos
    did all [
        val = 9
        pos = [fail "didn't stop"] 'pos
    ]
)

; ELIDE is not fully invisible, but trades this off to be able to run its
; code "in turn", instead of being slaved to eager enfix evaluation order.
;
; https://trello.com/c/snnG8xwW

(
    1 = do [elide "a" 1]
)
(
    1 = do [1 elide "a"]
)
(
    () = do [elide "a"]
)

(
    pos: _
    error? trap [
        do/next [1 elide "a" + elide "b" 2 * 3 fail "didn't stop"] 'pos
    ]
)
(
    pos: _
    error? trap [
        do/next [1 elide "a" elide "b" + 2 * 3 fail "didn't stop"] 'pos
    ]
)
(
    pos: _
    val: do/next [1 + 2 * 3 elide "a" elide "b" fail "didn't stop"] 'pos
    did all [
        val = 9
        pos = [fail "didn't stop"]
    ]
)


(
    unset 'x
    x: 1 + 2 * 3
    elide (y: :x)

    did all [x = 9 | y = 9]
)
(
    unset 'x
    x: 1 + elide (y: 10) 2 * 3
    did all [
        x = 9
        y = 10
    ]
)

(
    unset 'x
    unset 'y
    unset 'z

    x: 10
    y: 1 comment [+ 2
    z: 30] + 7

    did all [
        x = 10
        y = 8
        not set? 'z
    ]
)

(
    () = do [end]
)
(
    3 = do [1 + 2 end 10 + 20 | 100 + 200]
)
(
    ok? trap [eval (proc [x [<end>]] []) end 1 2 3]
)
(
    error? trap [eval (proc [x [<opt>]] []) end 1 2 3]
)

(
    [3 11] = reduce [1 + 2 elide 3 + 4 5 + 6]
)


; BAR! is invisible, and acts as an expression barrier

(
    3 = (1 + 2 |)
)(
    3 = (1 + 2 | comment "invisible")
)

; Non-variadic
[
    (
        left-normal: enfix right-normal:
            <- func [return: [<opt> bar!] x [bar!]] [:x]
        left-normal*: enfix right-normal*:
            <- func [return: [<opt> bar!] x [bar! <end>]] [:x]

        left-tight: enfix right-tight:
            <- func [return: [<opt> bar!] #x [bar!]] [:x]
        left-tight*: enfix right-tight*:
            <- func [return: [<opt> bar!] #x [bar! <end>]] [:x]

        left-soft: enfix right-soft:
            <- func [return: [<opt> bar!] 'x [bar!]] [:x]
        left-soft*: enfix right-soft*:
            <- func [return: [<opt> bar!] 'x [bar! <end>]] [:x]

        left-hard: enfix right-hard:
            <- func [return: [<opt> bar!] :x [bar!]] [:x]
        left-hard*: enfix right-hard*:
            <- func [return: [<opt> bar!] :x [bar! <end>]] [:x]

        true
    )

    ('no-arg = (trap [right-normal |])/id)
    (void? do [right-normal* |])
    (void? do [right-normal*])

    ('no-arg = (trap [| left-normal])/id)
    (void? do [| left-normal*])
    (void? do [left-normal*])

    ('no-arg = (trap [right-tight |])/id)
    (void? do [right-tight* |])
    (void? do [right-tight*])

    ('no-arg = (trap [| left-tight])/id)
    (void? do [| left-tight*])
    (void? do [left-tight*])

    ('no-arg = (trap [right-soft |])/id)
    (void? do [right-soft* |])
    (void? do [right-soft*])

    ('no-arg = (trap [| left-soft])/id)
    (void? do [| left-soft*])
    (void? do [left-soft*])

    ('| = do [right-hard |])
    ('| = do [right-hard* |])
    (void? do [right-hard*])

    ('| = do [| left-hard])
    ('| = do [| left-hard*])
    (void? do [left-hard*])
]


; Variadic
[
    (
        left-normal: enfix right-normal:
            <- func [return: [<opt> bar!] x [bar! <...>]] [take x]
        left-normal*: enfix right-normal*:
            <- func [return: [<opt> bar!] x [bar! <...> <end>]] [take* x]

        left-tight: enfix right-tight:
            <- func [return: [<opt> bar!] #x [bar! <...>]] [take x]
        left-tight*: enfix right-tight*:
            <- func [return: [<opt> bar!] #x [bar! <...> <end>]] [take* x]

        left-soft: enfix right-soft:
            <- func [return: [<opt> bar!] 'x [bar! <...>]] [take x]
        left-soft*: enfix right-soft*:
            <- func [return: [<opt> bar!] 'x [bar! <...> <end>]] [take* x]

        left-hard: enfix right-hard:
            <- func [return: [<opt> bar!] :x [bar! <...>]] [take x]
        left-hard*: enfix right-hard*:
            <- func [return: [<opt> bar!] :x [bar! <...> <end>]] [take* x]

        true
    )

    (error? trap [right-normal |])
    (void? do [right-normal* |])
    (void? do [right-normal*])

    (error? trap [| left-normal])
    (void? do [| left-normal*])
    (void? do [left-normal*])

    (error? trap [right-tight |])
    (void? do [right-tight* |])
    (void? do [right-tight*])

    (error? trap [| left-tight])
    (void? do [| left-tight*])
    (void? do [left-tight*])

    (error? trap [right-soft |])
    (void? do [right-soft* |])
    (void? do [right-soft*])

    (error? trap [| left-soft])
    (void? do [| left-soft*])
    (void? do [left-soft*])

    ('| = do [right-hard |])
    ('| = do [right-hard* |])
    (void? do [right-hard*])

    ('| = do [| left-hard])
    ('| = do [| left-hard*])
    (void? do [left-hard*])
]
