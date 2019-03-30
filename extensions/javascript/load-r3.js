//
// File: %load-r3.js
// Summary: "Single-File script for detecting and loading libRebol JS"
// Project: "JavaScript REPLpad for Ren-C branch of Rebol 3"
// Homepage: https://github.com/hostilefork/replpad-js/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright (c) 2018-2019 hostilefork.com
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The nature of WebAssembly and worker threads is such that the build
// products of emscripten produce more than one file.  The details of doing
// a load of those emscripten products can be somewhat daunting...so this file
// makes it possible to get everything taken care of by including only a
// single `<script>` tag on a page.
//
// Adding to the complexity is that the JavaScript extension is designed to be
// able to build versions of the code.  Both versions can accomplish I/O in
// a way that appears synchronous: using pthreads or using the "Emterpreter":
//
// https://emscripten.org/docs/porting/pthreads.html
// https://github.com/kripken/emscripten/wiki/Emterpreter
//
// pthreads rely on SharedArrayBuffer and WASM threading, and hence aren't
// ready in quite all JS environments yet.  However, the resulting build
// products are half the size of what the emterpreter makes--and around
// THIRTY TIMES FASTER.  Hence, the emterpreter is not an approach that is
// likely to stick around any longer than it has to.
//
// But for the foreseeable future, support for both is included, and this
// loader does the necessary detection to decide which version the host
// environment is capable of running.
//

//=////////////////////////////////////////////////////////////////////////=//
//
//  Example:
//  <body>
//  <div id="console_out"></div>
//  <!-- WARNING: load-r3.js URL MUST CONTAIN '/':
//  USE './load-r3.js' INSTEAD OF 'load-r3.js'"
//  -->
//  <script src="./load-r3.js"></script>
//  <script>
//    load_r3.then(() => {
//      console.log("Ready!")
//    })
//  </script>
//  </body>
//
//=////////////////////////////////////////////////////////////////////////=//
'use strict'  // <-- FIRST statement! https://stackoverflow.com/q/1335851


//=//// SIMULATED DEVELOPER CONSOLE INSIDE BROWSER WINDOW /////////////////=//
//
// Mobile web browsers frequently do not have the "Ctrl-Shift-I" option to
// open developer tools.  To assist in debugging, if the page you are loading
// the library from has a `#console_out` element, then we hook it up to mirror
// whatever gets written via console.log(), console.warn(), etc.

function try_set_console() {
    let console_out = document.getElementById("console_out")
    if (!console_out)
        return false  // DOM may not be loaded yet; we'll try this twice

    let rewired = function (old_handler, classname) {
        return (txt) => {
            let p = document.createElement("p")
            p.className = classname
            p.innerText = txt
            console_out.appendChild(p)
            console_out.scrollTop = console_out.scrollHeight

            old_handler(txt)  // also show message in browser developer tools
        }
    }

    console.info = rewired(console.info, "info")
    console.log = rewired(console.log, "log")
    console.warn = rewired(console.warn, "warn")
    console.error = rewired(console.error, "error")

    return true
}

if (!try_set_console()) {  // DOM may not have been loaded, try when it is
    document.addEventListener('DOMContentLoaded', try_set_console)
}



if (typeof WebAssembly !== "object") {
    throw Error("Your browser doesn't support WebAssembly.")
}

if (typeof Promise !== "function") {
    throw Error("Your browser doesn't support Promise.")
}

var hasShared = typeof SharedArrayBuffer !== "undefined"
console.info("Has SharedArrayBuffer => " + hasShared)

var hasThreads = false
if (hasShared) {
    let test = new WebAssembly.Memory({
        "initial": 0, "maximum": 0, "shared": true
    });
    hasThreads = (test.buffer instanceof SharedArrayBuffer)
}
console.info("Has Threads => " + hasThreads)

var use_emterpreter = ! hasThreads
var os_id = (use_emterpreter ? "0.16.1" : "0.16.2")

console.info("Use Emterpreter => " + use_emterpreter)

// WARNING: load-r3.js URL MUST CONTAIN '/':
// USE './load-r3.js' INSTEAD OF 'load-r3.js'"
var base_dir = document.querySelector('script[src$="/load-r3.js"]').src
base_dir = base_dir.substring(0, base_dir.indexOf("load-r3.js"))
// simulate remote url
// base_dir = "http://metaeducation.s3.amazonaws.com/travis-builds/"
if (base_dir == "http://metaeducation.s3.amazonaws.com/travis-builds/") {
    // correct http => https
    base_dir = "https://metaeducation.s3.amazonaws.com/travis-builds/"
}


var is_debug = false

// 'load' args from
var args = (location.search
    ? location.search.substring(1).split('&')
    : []
)
// process args
for (let i = 0; i < args.length; i++) {
    args[i] = decodeURIComponent(args[i])
    if (args[i] == 'debug') is_debug = true
}

if (is_debug) {
    var old_alert = window.alert
    window.alert = function(message) {
        console.error(message)
        old_alert(message)
        debugger
    }
}

// THE NAME OF THIS VARIABLE MUST BE SYNCED WITH
// https://metaeducation.s3.amazonaws.com/travis-builds/${OS_ID}/zzz_git_commit.js
// that contains `git_commit = ${GIT_COMMIT_SHORT}`
// See .travis.yml
//
var git_commit = ""

// Note these are "promiser" functions, because if they were done as a promise
// it would need to have a .catch() clause attached to it here.  This way, it
// can just use the catch of the promise chain it's put into.)

let load_js_promiser = (url) => new Promise(function(resolve, reject) {
    let script = document.createElement('script')
    script.src = url
    script.onload = () => {resolve(url)}
    script.onerror = () => {reject(url)}
    if (document.body) {
        document.body.appendChild(script)
    } else {
        document.addEventListener(
            'DOMContentLoaded',
            () => { document.body.appendChild(script) }
        )
    }
})

var git_commit_promiser = (os_id) => {
    if (base_dir == "https://metaeducation.s3.amazonaws.com/travis-builds/"
    ) { // load from amazonaws.com
        return load_js_promiser(
            base_dir + os_id + "/zzz_git_commit.js"
        )
    } else { return Promise.resolve(null)}
}

var lib_suffixes = [
    ".js", ".wasm",  // all builds
    ".wast", ".temp.asm.js",  // debug only
    ".bytecode",  // emterpreter builds only
    ".js.mem", ".worker.js"  // non-emterpreter builds only
]


// At this moment, there are 3 files involved in the download of the library:
// a .JS loader stub, a .WASM loader stub, and a large emterpreter .BYTECODE
// file.  See notes on the hopefully temporary use of the "Emterpreter",
// without which one assumes only a .wasm file would be needed.
//
// If you see files being downloaded multiple times in the Network tab of your
// browser's developer tools, this is likely because your webserver is not
// configured correctly to offer the right MIME type for the .wasm file...so
// it has to be interpreted by JavaScript.  See the README.md for how to
// configure your server correctly.
//
function libRebolComponentURL(suffix) {  // suffix includes the dot
    if (!lib_suffixes.includes(suffix))
        throw Error("Unknown libRebol component extension: " + suffix)

    if (use_emterpreter) {
        if (suffix == ".worker.js" || suffix == ".js.mem")
            throw Error(
                "Asking for " + suffix + " file "
                + " in an emterpreter build (should only be for pthreads)"
            )
    }
    else {
        if (suffix == ".bytecode")
            throw Error(
                "Asking for " + suffix + " file "
                + " in an emterpreter build (should only be for pthreads)"
            )
    }

    // When using pthread emulation, Emscripten generates `libr3.worker.js`.
    // You tell it how many workers to "pre-spawn" so they are available
    // at the moment you call `pthread_create()`, see PTHREAD_POOL_SIZE.  Each
    // worker needs to load its own copy of the libr3.js interface to have
    // the cwraps to the WASM heap available (since workers do not have access
    // to variables on the GUI thread).
    //
    // Due to origin policy restrictions, you typically need to have a
    // worker live in the same place your page is coming from.  To make Ren-C
    // fully hostable remotely it uses a hack of fetching the JS file via
    // CORS as a Blob, and running the worker from that.  An Emscripten change
    // that would be better than patching libr3.js post-build discussed here:
    //
    // https://github.com/emscripten-core/emscripten/issues/8338
    //
    if (false) {  // page-relative location not enforced due to workaround
        if (suffix == ".worker.js")
            return "libr3" + suffix
    }

    // !!! These files should only be generated if you are debugging, and
    // are optional.  But it seems locateFile() can be called to ask for
    // them anyway--even if it doesn't try to fetch them (e.g. no entry in
    // the network tab that tried and failed).  Review build settings to
    // see if there's a way to formalize this better to know what's up.
    //
    if (false) {
        if (suffix == ".wast" || suffix == ".temp.asm.js")
            throw Error(
                "Asking for " + suffix + " file "
                + " in a non-debug build (only for debug builds)")
    }

    let opt_dash = git_commit ? "-" : "";
    return base_dir + os_id + "/libr3" + opt_dash + git_commit + suffix
}


var Module = {
    //
    // For errors like:
    //
    //    "table import 1 has a larger maximum size 37c than the module's
    //     declared maximum 890"
    //
    // The total memory must be bumped up.  These large sizes occur in debug
    // builds with lots of assertions and symbol tables.  Note that the size
    // may appear smaller than the maximum in the error message, as previous
    // tables (e.g. table import 0 in the case above) can consume memory.
    //
    // !!! Messing with this setting never seemed to help.  See the emcc
    // parameter ALLOW_MEMORY_GROWTH for another possibility.
    //
 /* TOTAL_MEMORY: 16 * 1024 * 1024, */

    locateFile: function(s) {
        //
        // function for finding %libr3.wasm  (Note: memoryInitializerPrefixURL
        // for bytecode was deprecated)
        //
        // https://stackoverflow.com/q/46332699
        //
        console.info("Module.locateFile() asking for .wasm address of " + s)

        let stem = s.substr(0, s.indexOf('.'))
        let suffix = s.substr(s.indexOf('.'))

        // Although we rename the files to add the Git Commit Hash before
        // uploading them to S3, it seems that for some reason the .js hard
        // codes the name the file was built under in this request.  :-/
        // So even if the request was for `libr3-xxxxx.js` it will be asked
        // in this routine as "Where is `libr3.wasm`
        //
        // For the moment, sanity check to libr3.  But it should be `rebol`,
        // or any name you choose to build with.
        //
        if (stem != "libr3")
            throw Error("Unknown libRebol stem: " + stem)

        return libRebolComponentURL(suffix)
    },

    // This is a callback that happens sometime after you load the emscripten
    // library (%libr3.js in this case).  It's turned into a promise instead
    // of a callback.  Sanity check it's not used prior by making it a string.
    //
    onRuntimeInitialized: "<mutated from a callback into a Promise>",

    // If you use the emterpreter, it balloons up the size of the javascript
    // unless you break the emterpreter bytecode out into a separate file.
    // You have to get the data into the Module['emterpreterFile'] before
    // trying to load the emscripten'd code.
    //
    emterpreterFile: "<if `use_emterpreter`, fetch() of %libr3.bytecode>"

    // The rest of these fields will be filled in by the boilerplate of the
    // Emterpreter.js file when %libr3.js loads (it looks for an existing
    // Module and adds to it, but this is also how you parameterize options.)
}


//=// CONVERTING CALLBACKS TO PROMISES /////////////////////////////////////=//
//
// https://stackoverflow.com/a/22519785
//

let dom_content_loaded_promise = new Promise(function (resolve, reject) {
    //
    // The code for load-r3.js originally came from ReplPad, which didn't
    // want to start until the WASM code was loaded -AND- the DOM was ready.
    // It was almost certain that the DOM would be ready first (given the
    // WASM being a large file), but doing things properly demanded waiting
    // for the DOMContentLoaded event.
    //
    // Now that load-r3.js is a library, it's not clear if it should be its
    // responsibility to make sure the DOM is ready.  This would have to be
    // rethought if the loader were going to be reused in Node.js, since
    // there is no DOM.  However, if any of the loaded extensions want to
    // take the DOM being loaded for granted, this makes that easier.  Review.
    //
    document.addEventListener('DOMContentLoaded', resolve)
})

var onGuiInitialized = () => {} // back-compatibility

var runtime_init_promise = new Promise(function(resolve, reject) {
    //
    // The load of %libr3.js will at some point will trigger a call to
    // onRuntimeInitialized().  We set it up so that when it does, it will
    // resolve this promise (used to trigger a .then() step).
    //
    Module.onRuntimeInitialized = resolve
})


// If we are using the emterpreter, Module.emterpreterFile must be assigned
// before the %libr3.js starts running.  And it will start running some time
// after the dynamic `<script>` is loaded.
//
// See notes on short_hash_promiser for why this is a "promiser", not a promise
//
var bytecode_promiser
if (!use_emterpreter)
    bytecode_promiser = () => {
        console.info("Not emterpreted libr3.js, not requesting bytecode")
        return Promise.resolve()
    }
else {
    bytecode_promiser = () => {
        let url = libRebolComponentURL(".bytecode")
        console.info("Emterpreted libr3.js, requesting bytecode from:" + url)

        return fetch(url)
          .then(function(response) {

            // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
            if (!response.ok)
                throw Error(response.statusText)  // handled by .catch() below

            return response.arrayBuffer()  // arrayBuffer() method is a promise

          }).then(function(buffer) {

            Module.emterpreterFile = buffer  // must load before emterpret()-ing
          })
    }
}


// Initialization is written as a series of promises for, uh, "simplicity".
//
// !!! Review use of Promise.all() for steps which could be run in parallel.
//
var r3_ready_promise =
  git_commit_promiser(os_id) // set git_commit
  .then(bytecode_promiser)  // needs git_commit
  .then(function() {
      load_js_promiser(libRebolComponentURL(".js"))
  }).then(function() {
    console.info('Loading/Running ' + libRebolComponentURL(".js") + '...')
    if (use_emterpreter) {
      console.warn("Using Emterpreter is SLOW! Be patient...")
    }

    return runtime_init_promise

  }).then(function() {  // emscripten's onRuntimeInitialized() has no args

    console.info('Executing Rebol boot code...')
    reb.Startup()

    // There is currently no method to dynamically load extensions with
    // r3.js, so the only extensions you can load are those that are picked
    // to be built-in while compiling the lib.  The "JavaScript extension" is
    // essential--it contains JS-NATIVE and JS-AWAITER.
    //
    console.info('Initializing extensions')
    reb.Elide(
        "for-each collation builtin-extensions",
            "[load-extension collation]"
    )
  })

var load_r3 = r3_ready_promise // alias
