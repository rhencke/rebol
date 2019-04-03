; call/call.test.reb

; CALL/OUTPUT tests
; see - https://github.com/metaeducation/ren-c/issues/537
; and following fixes
; - https://github.com/metaeducation/ren-c/commit/298409f485420ecd03f0be4b465111be4ad829cd
; - https://github.com/metaeducation/ren-c/commit/e57c147465f3ed47f297e7a3ce3bb0319635f81f

(
    ; small - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 100}
    ] data: {}

    100 = length of data
)
(
    ; medium - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 9000}
    ] data: {}

    9000 = length of data
)
(
    ; large - note Windows doesn't do BLOCK! arg to CALL (argv style) yet

    call/shell/output spaced [
        (file-to-local system/options/boot)
        {--suppress "*" call/print.reb 80000}
    ] data: {}

    80'000 = length of data
)

; extra large CALL/OUTPUT (500K+), test only run if can find git binary
(
    (not exists? %/usr/bin/git) or [
        data: {}
        call/output compose [
            %/usr/bin/git "log" (spaced [
                "--pretty=format:'["
                    "commit: {%h}"
                    "author: {%an}"
                    "email: {%ae}"
                    "date-string: {%ai}"
                    "summary: {%s}"
                "]'"
            ])
        ] data
        (500'000 < length of data) and [
            did find data "summary: {Initial commit}"
        ]
    ]
)

; Tests feeding input and taking output from various sources
[
    (did echoer: enclose specialize 'call/input/output [
        command: spaced [
            file-to-local system/options/boot {--suppress "*"} {-qs}
            {--do} {"write-stdout read system/ports/input"}
        ]
    ] function [frame [frame!]] [
        out: frame/output
        do frame
        return out
    ])

    ("Foo" = echoer "Foo" "")
    ("Rҽʋσʅυƚισɳ" = echoer "Rҽʋσʅυƚισɳ" "")
    ("One^/Two" = echoer "One^/Two" "")

    (#{466F6F} = echoer #{466F6F} #{})
    ("Foo" = echoer #{466F6F} "")
    (#{466F6F} = echoer "Foo" #{})
    (#{DECAFBAD} = echoer #{DECAFBAD} #{})
    (error? trap [#{DECAFBAD} = echoer #{DECAFBAD} ""])
]

; Both unix and windows echo text back, so this is a good test of the shell
; But line endings will vary because it's not redirected.  :-/
(
    call/shell/output "echo test" out: ""
    did any [
        "test^M^/" = out
        "test^/" = out
    ]
)
