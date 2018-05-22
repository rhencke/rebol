; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! = type of blank)
; literal form
(blank = _)
[#845
    (blank = _)
]
(blank = #) ;-- Deprecated!
(blank = make blank! blank)
(null? to blank! blank) ;-- conflicting needs; blank in void out general rule
(blank = to blank! 1)
("_" = mold blank)
[#1666 #1650 (
    f: does [#]
    # == f
)]
