; datatypes/unset.r
(null? ())
(null? type of ())
(not null? 1)

[#68
    ('need-value = (trap [a: ()])/id)
]

(error? trap [set* quote a: () a])
(not error? trap [set* 'a ()])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value]
    e/id = 'no-value
)
