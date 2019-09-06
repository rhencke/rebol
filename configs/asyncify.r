REBOL [
    File: %asyncify.r

    Description: {
        The inability to communicate synchronously between the worker and GUI
        in JavaScript means that being deep in a C-based interpreter stack on
        the  worker cannot receive data from the GUI.  "Asyncify" augments 
        the webassembly code, permitting it to `emscripten_sleep()` long
        enough to release the GUI thread it's running on:

        https://emscripten.org/docs/porting/asyncify.html

        It's a stopgap measure, which makes the build products twice as large
        and somewhat slower (but replaces an earlier *much* slower approach
        using a bytecode interpreter, called "the Emterpreter").  Ultimately
        Asyncify will be supplanted entirely as well, with the default approach
        which is based on WASM threads.  In that model, the GUI thread is left
        free, while the code that's going to make demands runs on its own
        thread...which can suspend and wait, using conventional atomics
        (mutexes, wait conditions).

        There was some issue with WASM threading being disabled in 2018 due to
        Spectre vulnerabilities in SharedArrayBuffer.  This seems to be
        mitigated, and approaches are now focusing on assuming that the
        thread-based solution will be available.  The asyncify method is
        preserved as a fallback, but should not be used without good reason--
        only basic features are implemented for this config.
    }
]

config: %emscripten.r  ; Inherit most settings from this config

os-id: 0.16.1  ; JS, web, no threads

; Right now, either #web or #node
;
javascript-environment: #web

use-asyncify: true

use-wasm: true
