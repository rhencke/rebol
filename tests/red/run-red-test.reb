Rebol [
    Title:   "Driver for Running Red-Lang Tests from Rebol3/Ren-C"
    File:    %run-red-test.reb
    Rights:  "Copyright (C) 2019 Rebol Open Source Contribuors"
    License: "BSD-3 https://github.com/red/red/blob/origin/BSD-3-License.txt"

    Description: {
        The Red Language project is a Rebol-derivative which largely follows
        conventions from Rebol2:

        http://red-lang.org

        At a language level, Ren-C seeks to be flexible enough to emulate
        conventions from Rebol2, Red, or elsewhere.  So this driver is part
        of an initiative to put the interpreter into "Redbol" emulation and be
        able to execute the tests that are included in the Red project's
        repository on GitHub.

        https://github.com/red/red/tree/master/tests/source/units

        Because a large number of tests will not succeed in the near term,
        the idea is to maintain lists of test names that are known to not
        work and keep those lists in files with the same base name as the
        Red test file.
    }
]

none: _

~~~start-file~~~: function [name [text!]] [
    print ["~~~~start-file~~~~" name]
]

~~~end-file~~~: function [] [
    print ["~~~~end-file~~~~"]
]

groupname: _

===start-group===: function [name [text!] <with> groupname] [
    assert [not groupname]
    groupname: name
    print newline
    print [{=== START GROUP} mold groupname {===}]
]

===end-group===: function [<with> groupname] [
    print newline
    print [{=== END GROUP} mold groupname {===}]
    groupname: _
]

runner: function [
    name [blank! text!]
    expr [<opt> any-value! <...>]
    :look [<opt> any-value! <...>]
][
    if name [
        write-stdout unspaced ["#" name space]
    ]

    any-newlines: _
    expression: collect [
        while [not find [--assert --test-- ===end-group===] first look] [
            line: either new-line? look [/line] [_]
            any-newlines: me or @line
            keep/only/(line) take look
        ]
    ]

    if any-newlines [  ; ensure newlines on head/tail of collected values
        new-line tail expression true
    ]

    write-stdout mold as group! expression
    write-stdout space

    return trap [return do expression]
]

--test--: chain [
    adapt 'runner [
        print newline
    ]
        |
    function [result [<opt> any-value!]] [
        print newline
    ]
]

--assert: chain [
    specialize :runner [name: _]
        |
    function [result [<opt> any-value!]] [
        if error? result [
            print spaced [{failed, error id:} result/id]
        ] else [
            if result = true [
                print mold "success"
            ] else [
                print mold spaced [
                    "failed, return was"
                    either result = false ["false"] [type of result]
                ]
            ]
        ]
    ]
]
