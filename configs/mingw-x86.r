REBOL [
    File: %mingw-x86.r
]

os-id: 0.3.1

toolset: [
    gcc %i686-w64-mingw32-gcc
    ld %i686-w64-mingw32-gcc  ; Linking is done via calling gcc, not ld
    strip %i686-w64-mingw32-strip
]
