[
    ; Fixed file, content copied from:
    ; https://www.w3.org/2001/06/utf-8-test/UTF-8-demo.html
    ;
    ; Its length can be verified via Python2 with:
    ;
    ;     import codecs
    ;     with codecs.open('utf8-plain-text.txt', encoding='utf-8') as myfile:
    ;         data = myfile.read()
    ;         print(len(data))
    (
        t: to text! read %fixtures/utf8-plain-text.txt
        tlen: length of t
        assert [tlen = 7086]

        braille: "⡕⠇⠙ ⡍⠜⠇⠑⠹ ⠺⠁⠎ ⠁⠎ ⠙⠑⠁⠙ ⠁⠎ ⠁ ⠙⠕⠕⠗⠤⠝⠁⠊⠇⠲"

        assert [37 = length of braille]

        warning: "⚠"
        assert [1 = length of warning]

        true
    )

    (
        tcopy: copy t
        replace tcopy braille null
        (length of tcopy) = (tlen - length of braille)
    )

    (
        tcopy: copy t
        replace tcopy braille warning
        (length of tcopy) = (tlen + 1 - length of braille)
    )

    (
        tcopy: copy t
        pos: find tcopy braille
        change/part pos warning length of braille
        assert [pos/1 = to char! warning]
        (length of tcopy) = (tlen + 1 - length of braille)
    )

    (
        tcopy: copy t
        n: 0
        while [c: take* tcopy] [
            n: n + 1
            assert [c = t/(n)]
        ]
        n = tlen
    )

    (
        tcopy: copy t
        n: length of t
        while [c: take*/last tcopy] [
            assert [c = t/(n)]
            n: n - 1
        ]
        n = 0
    )

    (
        n: 0
        for-each c t [
            n: n + 1
            assert [c = t/(n)]
        ]
        n = tlen
    )

    (
        assert [parse t [to braille copy b to newline to end]]
        b = braille
    )
]


(
    str: "caffè"
    bin: as binary! str
    append bin 65
    did all [
        bin = #{63616666C3A841}
        str = "caffèA"
    ]
)
