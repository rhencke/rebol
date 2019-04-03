; better-than-nothing (New)APPLY tests

(
    s: applique :append [
        series: [a b c]
        value: [d e]
        dup: 2
    ]
    s = [a b c d e d e]
)
