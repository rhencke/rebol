;
; MATCH started out as a userspace function, but gained frequent usage...and
; also has an interesting variadic feature that is not (yet) available to
; userspace VARARGS!.  This enables it to intercept the first argument of a
; filter function, that is invoked.
;
; !!! Because of its variadic implementation mechanism, there is currently
; an issue with MATCH and writing `match parse "aaa" [some "a"] = "aaa"`.
; The problem is ambiguous variadic deferment; known and being researched...
;
; https://github.com/metaeducation/ren-c/pull/730
;

("aaa" = match parse "aaa" [some "a"])
(null = match parse "aaa" [some "b"])

(10 = match integer! 10)
(null = match integer! "ten")

("ten" = match [integer! text!] "ten")
(20 = match [integer! text!] 20)
(null = match [integer! text!] <tag>)

(10 = match :even? 10)
(null = match :even? 3)
(null = match 'odd? 20)
(7 = match 'odd? 7)

(bar? match blank! _)
(null = match blank! 10)
(null = match blank! false)

; Since its other features were implemented with a fairly complex enclosed
; specialization, it's good to keep that usermode implementation around,
; just to test those features.

[
    (did match2: enclose specialize 'either-test [
        branch: [] ;-- runs on test failure
        opt: true ;-- failure branch returns void, signals the enclosure
    ] function [
        return: [<opt> any-value!]
        f [frame!]
    ][
        if null? arg: :f/arg [
            fail "MATCH cannot take null as input" ;-- EITHER-TEST allows it
        ]

        ; Ideally we'd pass through all input results on a "match" and give
        ; blank to indicate a non-match.  But what about:
        ;
        ;     if match [logic!] 1 > 2 [...]
        ;     if match [blank!] find "abc" "d" [...]
        ;
        ; Rather than have MATCH return a falsey result in these cases, pass
        ; back a BAR!.  But on failure, pass back a null.  That will cue
        ; attention to the distorted success result.

        set* quote result: do f ;-- can't access f/arg after the DO

        if not :arg and (not null? :result) [
            return '| ;-- BAR! if matched a falsey type
        ]
        to-value :result ;-- return blank if no match, else truthy result
    ])

    (10 = match2 integer! 10)
    (_ = match2 integer! "ten")
    (bar? match2 blank! _)
    (_ = match2 blank! 10)
]
