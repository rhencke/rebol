; Unlike PROTECT which can be used to protect a series regardless of which
; value is viewing it, CONST is a trait of a value that views a series.
; The same series can have const references and mutable references to it.

(
    data: mutable [a b c]
    data-readonly: const data
    did all [
        (e: trap [append data-readonly <readonly>] e/id = 'const-value)
        append data <readwrite>
        data = [a b c <readwrite>]
        data-readonly = [a b c <readwrite>]
    ]
)(
    sum: 0
    e: trap [
        loop 5 code: [
            ()
            append code/1 [sum: sum + 1]
        ]
    ]
    e/id = 'const-value
)(
    sum: 0
    loop 5 code: [
        ()
        append mutable code/1 [sum: sum + 1]
    ]
    sum = 10
)


; ACTION! definitions which occur inside a DO/MUTABLE will cause mutability to
; be activated when they are invoked, whether they are invoked from a frame
; with mutable or immutable conventions.  This enables Rebol2-style functions
; to be called from const-styled Ren-C without breaking their internal rules.
(
    foo: function [] [b: [1 2 3] clear b]
    e: trap [foo]
    e/id = 'const-value
)(
    do mutable [foo: function [] [b: [1 2 3] clear b]]
    [] = foo
)(
    foo: do mutable [function [] [b: [1 2 3] clear b]]
    [] = foo
)(
    [] = do mutable [
        foo: function [] [b: [1 2 3] clear b]
        foo
    ]
)(
    do mutable [foo: function [b] [clear b]]
    e: trap [foo [1 2 3]] ;; const value passed in doesn't get mutable
    e/id = 'const-value
)(
    do mutable [foo: function [b] [clear b]]
    [] = do mutable [foo [1 2 3]] ;; okay if passed from mutable section
)


(
    [<succeed>] = do mutable compose [append [] <succeed>]
)(
    block: [] ;; originates from outside mutable
    e: trap [
        do mutable compose [append ((block)) <fail>]
    ]
    e/id = 'const-value
)

;; A shallow COPY of a literal value that the evaluator has made const will
;; only make the outermost level mutable...referenced series will be const
;; if they weren't copied (and weren't mutable explicitly)
(
    data: copy [a [b [c]]]
    append data <success>
    e2: trap [append data/2 <fail>]
    e22: trap [append data/2/2 <fail>]
    did all [
        data = [a [b [c]] <success>]
        e2/id = 'const-value
        e22/id = 'const-value
    ]
)(
    data: copy/deep [a [b [c]]]
    append data <success>
    append data/2 <success>
    append data/2/2 <success>
    data = [a [b [c <success>] <success>] <success>]
)(
    sub: [b [c]]
    data: copy compose [a ((mutable sub))]
    append data <success>
    append data/2 <success>
    append data/2/2 <success>
    data = [a [b [c <success>] <success>] <success>]
)
