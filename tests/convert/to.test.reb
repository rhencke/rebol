; functions/convert/to.r
[#38
    ('logic! = to word! logic!)
]
('percent! = to word! percent!)
('money! = to word! money!)
[#1967
    (not same? to binary! [1] to binary! [2])
]

; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
([a b c] = to block! 'a/b/c)
(lit (a b c) = to group! 'a/b/c)
([a b c] = to block! lit (a b c))
(lit (a b c) = to group! [a b c])
(lit a/b/c = to path! [a b c])
(lit a/b/c = to path! lit (a b c))

; Single-character strings and words can TO-convert to CHAR!
[
    (#"x" = to char! 'x)
    ('bad-cast = pick trap [to char! 'xx] 'id)

    (#"x" = to char! "x")
    ('bad-cast = pick trap [to char! 'xx] 'id)

    ; !!! Should this be rethought, e.g. to return NULL?
    ('bad-cast = pick trap [to char! ""] 'id)
]
