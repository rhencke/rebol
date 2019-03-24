REBOL [
    Title: "JavaScript Natives Usermode Support Code"

    Name: Javascript
    Type: Module
    Options: [isolate]

    Version: 0.1.0
    Date: 15-Sep-2018

    Rights: "Copyright (C) 2018-2019 hostilefork.com"

    License: {LGPL 3.0}
]

init-javascript-extension


; !!! The table which maps ID numbers to JavaScript functions that implement
; JS-NATIVEs has to live on the GUI thread.  But when running rebPromise(),
; that Rebol code runs on the worker thread...which includes the ordinary
; native for registering JavaScript natives.
;
; As a temporary workaround, this makes JS-NATIVE JavaScript code itself.
; That does enough to jump it to the main thread, where it calls the form
; of the main thread internal native creator.
;
js-native: js-native-mainthread [
    {Create ACTION! from textual JavaScript code}

    return: [action!]
    spec "Function specification (similar to the one used by FUNCTION)"
        [block!]
    source "JavaScript code as a text string" [text!]
    /awaiter "implicit resolve()/reject() parameters signal return result"
]{
    return reb.Run(
        "js-native-mainthread/(", reb.ArgR("awaiter"), ")",
        reb.ArgR("spec"), reb.ArgR("source")
    );
}

js-awaiter: specialize 'js-native [awaiter: true]

sys/export [js-native js-awaiter]  ; !!! Hacky export scheme
