; datatypes/lit-path.r
(lit-path? first ['a/b])
(not lit-path? 1)
(uneval path! = type of first ['a/b])
; minimum
[#1947
    (lit-path? uneval load "#[path! [[a] 1]]")
]
(
    all [
        lit-path? a: uneval load "#[path! [[a b c] 2]]"
        2 == index? a
    ]
)
; lit-paths are active
(
    a-value: first ['a/b]
    strict-equal? as path! dequote :a-value do reduce [:a-value]
)
