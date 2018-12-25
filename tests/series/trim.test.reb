; functions/series/trim.r

; refinement order
[#83
    (strict-equal?
        trim/all/with mutable "a" "a"
        trim/with/all mutable "a" "a"
    )
]

[#1948
    ("foo^/" = trim mutable "  foo ^/")
]

(#{BFD3} = trim mutable #{0000BFD30000})
(#{10200304} = trim/with mutable #{AEAEAE10200304BDBDBD} #{AEBD})

; Incompatible refinement errors.
[
    (did s: copy {})

    (error? trap [trim/auto/head s])
    (error? trap [trim/auto/tail s])
    (error? trap [trim/auto/lines s])
    (error? trap [trim/auto/all s])
    (error? trap [trim/all/head s])
    (error? trap [trim/all/tail s])
    (error? trap [trim/all/lines s])
    (error? trap [trim/auto/with s {*}])
    (error? trap [trim/head/with s {*}])
    (error? trap [trim/tail/with s {*}])
    (error? trap [trim/lines/with s {*}])

    (s = {})
]

("a  ^/  b  " = trim/head mutable "  a  ^/  b  ")
("  a  ^/  b" = trim/tail mutable "  a  ^/  b  ")
("foo^/^/bar^/" = trim mutable "  foo  ^/ ^/  bar  ^/  ^/  ")
("foobar" = trim/all mutable "  foo  ^/ ^/  bar  ^/  ^/  ")
("foo bar" = trim/lines mutable "  foo  ^/ ^/  bar  ^/  ^/  ")
("x^/" = trim/auto mutable "^/  ^/x^/")
("x^/" = trim/auto mutable "  ^/x^/")
("x^/y^/ z^/" = trim/auto mutable "  x^/ y^/   z^/")
("x^/y" = trim/auto mutable "^/^/  x^/  y")

([a b] = trim mutable [a b])
([a b] = trim mutable [a b _])
([a b] = trim mutable [_ a b _])
([a _ b] = trim mutable [_ a _ b _])
([a b] = trim/all mutable [_ a _ b _])
([_ _ a _ b] = trim/tail mutable [_ _ a _ b _ _])
([a _ b _ _] = trim/head mutable [_ _ a _ b _ _])
([a _ b] = trim/head/tail mutable [_ _ a _ b _ _])
