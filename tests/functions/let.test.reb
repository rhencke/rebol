; LET is Ren-C's initiative to try and make FUNC and FUNCTION synonyms
;
; https://forum.rebol.info/t/rethinking-auto-gathered-set-word-locals/1150

(
    y: <global>
    plus1000: func [j] [let b: 1000 | b + j]
    did all [
        1020 = plus1000 20
        [j] = parameters of :plus1000 
        [return j b] = words of make frame! :plus1000
    ] 
)
