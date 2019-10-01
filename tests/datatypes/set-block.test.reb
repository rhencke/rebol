; SET-BLOCK! tests

(set-block! = type of first [[a b c]:])
(set-path! = type of first [a/[b c d]:])

(
    a: _ b: _
    [a b]: [10 20]
    (a = 10) and [b = 20]
)(
    a: _ b: _
    [a b]: <thing>
    (a = <thing>) and [b = <thing>]
)
