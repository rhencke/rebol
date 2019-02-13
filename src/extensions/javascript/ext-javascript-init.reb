REBOL [
    Title: "JavaScript Natives Usermode Support Code"

    Name: Javascript
    Type: Module
    Options: [isolate]

    Version: 0.1.0
    Date: 15-Sep-2018

    Rights: "Copyright (C) 2018 Rebol Open Source Contributors"

    License: {Apache 2.0}
]

comment [
    {Rebol Support Routines for User Natives Would Go Here}
]

; Currently all the necessary initialization for JavaScript extensions is in
; the rebStartup() wrapper for the library.  But it's helpful to have a call
; here at least so that one can know in the debugger if the extension ran.
;
init-javascript-extension

js-awaiter: specialize 'js-native [awaiter: true]

sys/export [js-awaiter]  ; !!! Hacky export scheme
