; functions/series/sort.r
([1 2 3] = sort mutable [1 3 2])
([3 2 1] = sort/reverse mutable [1 3 2])
[#1152 ; SORT not stable (order not preserved)
    (strict-equal? ["A" "a"] sort mutable ["A" "a"])
]
[#1152 ; SORT not stable (order not preserved)
    (strict-equal? ["A" "a"] sort/reverse mutable ["A" "a"])
]
[#1152 ; SORT not stable (order not preserved)
    (strict-equal? ["a" "A"] sort mutable ["a" "A"])
]
[#1152 ; SORT not stable (order not preserved)
    (strict-equal? ["A" "a"] sort/case mutable ["a" "A"])
]
[#1152 ; SORT not stable (order not preserved)
    (strict-equal? ["A" "a"] sort/case mutable ["A" "a"])
]
[#1152 ; SORT not stable (order not preserved)
    (
    set [c d] sort reduce [a: "a" b: "a"]
    all [
        same? c a
        same? d b
        not same? c b
        not same? d a
    ]
    )
]
[#1152 ; SORT not stable (order not preserved)
    (equal? [1 9 1 5 1 7] sort/skip/compare mutable [1 9 1 5 1 7] 2 1)
]
([1 2 3] = sort/compare mutable [1 3 2] :<)
([3 2 1] = sort/compare mutable [1 3 2] :>)
[#1516 ; SORT/compare ignores the typespec of its function argument
    (error? trap [sort/compare reduce [1 2 _] :>])
]
