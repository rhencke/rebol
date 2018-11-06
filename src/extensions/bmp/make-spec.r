REBOL []

name: 'BMP
source: %bmp/ext-bmp.c
modules: [
    [
        name: 'BMP
        source: %bmp/mod-bmp.c
        includes: [
            %prep/extensions/bmp
        ]
    ]
]

