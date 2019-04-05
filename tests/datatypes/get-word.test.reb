; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! = type of first [:a])
(
    ; context-less get-word
    e: trap [do make block! ":a"]
    e/id = 'not-bound
)
(
    unset 'a
    null? :a
)

[#1477
    ((match get-path! lit :/) = (load ":/"))

    ((match get-path! lit ://) = (load "://"))

    ((match get-path! lit :///) = (load ":///"))
]
