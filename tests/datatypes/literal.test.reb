;; Ren-C's LITERAL! is a generic and arbitrary-depth variant of the
;; LIT-XXX! types from historical Rebol.  It uses a backslash instead of
;; a tick mark to do its escaping.

(
    x: 10
    set \x: 20
    x = 20
)(
    x: 10
    y: _
    foo: function [] [
        set \x: 20 
        set \y x
    ]
    foo
    x = 10 and (y = 20)
)

(null? \)
(null? do [\])
([\] = reduce [\\])
([\\] = reduce [\\\])
([\ \\ \\\ \\\\] = reduce [\\ \\\ \\\\ \\\\\])

(
    [1 (2 + 3) [4 + 5] a/+/b c/+/d: :e/+/f]
    = reduce
    [\1 \(2 + 3) \[4 + 5] \a/+/b \c/+/d: \:e/+/f]
)

(quote \[a b c] = uneval [a b c])
(quote \(a b c) == uneval quote (a b c))
(not (quote \[A B C] == uneval [a b c]))
(\\\[a b c] != \\\\\[a b c])
(\\\[a b c] = \\\[a b c])

(kind of quote \foo = literal!) ;; low level "KIND"
(type of quote \foo = lit word!) ;; higher-level "TYPE"
(type of quote \\[a b c] = lit lit block!)


;; Some generic actions have been tweaked to know to extend their
;; behavior and incorporate escaping into their results.  This is
;; not necessarily such a "weird" idea, given that you could do
;; things like append to a LIT-PATH!.  However, it should be
;; controlled by something in the function spec vs. be a random
;; list that added the behavior.

(quote \\\\3 = add quote \\\\1 2)

(quote \\\[b c d] = find \\\\[a b c d] \b)

(null = find \\\\[a b c d] \q)

(quote \\\[a b c] = copy quote \\\[a b c])

(quote \(1 2 3 <four>) = append mutable \\(1 2 3) <four>)


;; Routines could be adapted to do all kinds of interesting things
;; with the escaping, but COMPOSE goes ahead and knocks one level
;; of escaping off of any GROUP! it sees.

(
    compose [(1 + 2) \(1 + 2) \\(1 + 2)]
    == [3 (1 + 2) \(1 + 2)]
)(
    compose/deep [a \\[b (1 + 2) c] d]
    == [a \\[b 3 c] d] 
)
