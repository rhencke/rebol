REBOL []

name: 'Stdio
source: %stdio/mod-stdio.c
includes: [
    %prep/extensions/stdio
]

depends: compose [
    (switch system-config/os-base [
        'Windows [
            [%stdio/stdio-windows.c]
        ]

        default [
            [%stdio/stdio-posix.c]
        ]
    ])
]

