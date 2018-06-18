; datatypes/none.r
(blank? blank)
(not blank? 1)
(blank! = type of blank)
; literal form
(blank = _)
[#845
    (blank = _)
]
(blank = make blank! blank)
(blank? to blank! blank) ;-- can't follow "blank in, null out" rule
(blank = to blank! 1)
("_" = mold blank)
[#1666 #1650 (
    f: does [_]
    _ == f
)]
