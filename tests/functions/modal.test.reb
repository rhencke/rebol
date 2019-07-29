; Modal parameters are described here:
; https://forum.rebol.info/t/1187

[
    "Basic operational test"

    (did foo: function [@x /y] [
        reduce [x y]
    ])

    ([3 _] = foo 3)
    ([3 /y] = foo @(1 + 2))
    ([@(1 + 2) _] = foo '@(1 + 2))

    (did item: 300)

    ([304 _] = foo item + 4)
    ([304 /y] = foo @(item + 4))
    ([@(item + 4) _] = foo '@(item + 4))

    ([300 _] = foo item)
    ([300 /y] = foo @item)
    ([@item _] = foo '@item)

    ([[a b] _] = foo [a b])
    ([[a b] /y] = foo @[a b])
    ([@[a b] _] = foo '@[a b])

    (did obj: make object! [field: 1020])

    ([1020 _] = foo obj/field)
    ([1020 /y] = foo @obj/field)
    ([@obj/field _] = foo '@obj/field)
]

[
    "Basic infix operational test"

    (did bar: enfix function [@x /y] [
        reduce [x y]
    ])

    (3 bar = [3 _])
    (@(1 + 2) bar = [3 /y])

    (did item: 300)

    ((item + 4) bar = [304 _])
    (@(item + 4) bar = [304 /y])

    (item bar = [300 _])
    (@item bar = [300 /y])

    ([a b] bar = [[a b] _])
    (@[a b] bar = [[a b] /y])

    (did obj: make object! [field: 1020])

    (obj/field bar = [1020 _])
    (@obj/field bar = [1020 /y])
]

[
    "Demodalizing specialization test"

    (did foo: function [a @x /y] [
        reduce [a x y]
    ])

    ([a @x /y] = parameters of :foo)

    ([10 20 _] = foo 10 20)
    ([10 20 /y] = foo 10 @(20))

    (did fooy: :foo/y)

    ([a x] = parameters of :fooy)
    ([10 20 /y] = fooy 10 20)
    (
        'bad-refine = (trap [
            fooy/y 10 20
        ])/id
    )
    (
        'bad-refine = (trap [
            fooy 10 @(20)
        ])/id
    )
]
