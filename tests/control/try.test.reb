; functions/control/try.r
(
    e: trap [1 / 0]
    e/id = 'zero-divide
)
(
    success: true
    error? trap [
        1 / 0
        success: false
    ]
    success
)
(
    success: true
    f1: does [
        1 / 0
        success: false
    ]
    error? trap [f1]
    success
)
; testing TRY/EXCEPT
[#822
    (error? trap/with [make error! ""] [0])
]
(trap/with [fail make error! ""] [true])
(trap/with [1 / 0] :error?)
(trap/with [1 / 0] func [e] [error? e])
(trap/with [true] func [e] [false])
[#1514
    (error? trap [trap/with [1 / 0] :add])
]

[#1506 ((
    10 = eval func [] [trap [return 10] 20]
))]
