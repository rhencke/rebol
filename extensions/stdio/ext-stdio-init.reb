REBOL [
    Title: "Standard Input/Output"
    Name: StdIO
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

register-stdio-device

; During boot, there shouldn't be any output.  However, it would be annoying
; to have to write different versions of every PRINT-based debug routine out
; there which might be used after boot, just because it might be used before.
;
; We use HIJACK because if we just overwrote LIB/WRITE-STDOUT with the new
; function, it would not affect existing specializations and usages.

hijack 'lib/write-stdout :write-stdout
