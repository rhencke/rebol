REBOL []

name: 'Clipboard
source: %clipboard/ext-clipboard.c
modules: [
    [
        name: 'Clipboard
        source: [
            %clipboard/mod-clipboard.c
        ]
        includes: [
            %prep/extensions/clipboard ;for %tmp-ext-clipboard-init.inc
        ]
        libraries: _
    ]
]

options: []
