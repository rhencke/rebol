; frame.test.reb

(
    foo: function [return: [block!] arg] [
       local: 10
       frame: binding of 'return
       return words of frame
    ]

    did all [
       [arg] = parameters of :foo  ; doesn't expose locals
       [return arg local frame] = foo 20  ; exposes locals as WORD!s
    ]
)
