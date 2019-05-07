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
//=//// EXAMPLE ///////////////////////////////////////////////////////////=//
//
//  <body>
//      /* optional Rebol scripts: */
//      <script type="text/rebol" src="file.reb">
//          ...optional Rebol code...
//      </script>
//      ....
//
//      <!-- URL -----v must contain a `/` -->
//      <script src="./load-r3.js">
//          /* primary optional JS code */
//          let msg = "READY!"
//          console.log(
//              reb.Spell("spaced [",
//                  {reb.Xxx() API functions are now...}", reb.T(msg),
//              "]")
//          ) 
//      </script>
//      <script>
//          reb.Startup({...})  /* pass in optional configuration object */
//              .then(() => {...})  /* secondary optional JS code */
//      </script>
//  </body>
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * At time of writing, a hosted version of load-r3.js and the WebAssembly
//   build products is available at:
//
//   https://metaeducation.s3.amazonaws.com/travis-builds/load-r3.js
//
// * As noted in the comment in the example, the URL for %load-r3.js currently
//   must contain a `/`.  So use './load-r3.js' INSTEAD OF 'load-r3.js'"
//
// * This file is supposed to be able to load multiple versions of the
//   evaluator.  While it is still early at time of writing to say that
//   "it shouldn't have breaking protocol changes", over the long run it
//   really shouldn't...so try to keep its dependencies simple as possible.
//
// * Loading "modules" in JavaScript is an inexact science to begin with, and
//   it goes without saying that working with WebAssembly and Emscripten makes
//   things a lot more..."organic".  If you're the sort of person who knows
//   how to make the load process more rigorous, your insights would be
//   highly valued--so please do make suggestions, no matter how small.
//

'use strict'  // <-- FIRST statement! https://stackoverflow.com/q/1335851


//=//// ENTIRE SCRIPT IS WRAPPED IN REB.STARTUP() FUNCTION ////////////////=//
//
// This script only exports one function.  So we can use the function itself
// as the "module pattern", instead of an anonymous closure:
//
// https://medium.com/@tkssharma/javascript-module-pattern-b4b5012ada9f
//
// Two global objects are exported.  One is `Module`, which is how Emscripten
// expects to get its configuration parameters (as properties of that global
// object), so the startup function must initialize it with all the necessary
// properties and callbacks.
//
// The other object is `reb` which is the container for the API.  The reason
// all the APIs are in an object (e.g. `reb.Elide()` instead of `rebElide()`)
// is because Node.js doesn't allow global functions, so the only way to get
// an API would be through something like `let reb = require('rebol')`.
//
// It may look like reb.Startup() takes two parameters.  But if you read all
// the way to the bottom of the file, you'll see `console` is passed in with
// `bind` so that it can be overridden by a local variable with that name.
// The override helps us make sure we don't accidentally type something like
// `console.log` and not redirect through the `config.log` function.
//
// !!! In some editors (like Visual Studio) it seems impossible to stop it
// from indenting due to this function, no matter how many settings you turn
// off.  If you have that problem, comment out the function temporarily.
//

var reb = {}  // This aggregator is where we put all the Rebol APIs

var Module  // Emscripten expects this to be global and set up with options

reb.Startup = function(console_in, config_in) {  // only ONE arg, see above!


//=//// CONFIGURATION OBJECT //////////////////////////////////////////////=//
//
// More options will be added in the future, but for starters we let you
// hook the status messages that are sent to the console by default.  This is
// important for debugging in mobile browsers especially, where access to the
// console.log() output may not be available via Ctrl-Shift-I or otherwise.

const default_config = {
    log: console_in.log,
    info: console_in.info,
    error: console_in.error,
    warn: console_in.warn
}

let console = undefined;  // force use e.g. of config.log(), not console.log()

// Mimic jQuery "extend" (non-deeply) to derive from default config
// https://stackoverflow.com/a/39188108/211160
//
var config
if (config_in)  // config is optional, you can just say `load_r3()`
    config = Object.assign({}, default_config, config_in)
else
    config = default_config


//=//// PICK BUILD BASED ON BROWSER CAPABILITIES //////////////////////////=//
//
// The JavaScript extension can be built two different ways for the browser.
// Both versions can accomplish I/O in a way that appears synchronous: using
// pthreads or using the "Emterpreter":
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

if (typeof WebAssembly !== "object")
    throw Error("Your browser doesn't support WebAssembly.")

if (typeof Promise !== "function")
    throw Error("Your browser doesn't support Promise.")

let hasShared = typeof SharedArrayBuffer !== "undefined"
config.info("Has SharedArrayBuffer => " + hasShared)

let hasThreads = false
if (hasShared) {
    let test = new WebAssembly.Memory({
        "initial": 0, "maximum": 0, "shared": true
    });
    hasThreads = (test.buffer instanceof SharedArrayBuffer)
}
config.info("Has Threads => " + hasThreads)

let use_emterpreter = ! hasThreads
let os_id = (use_emterpreter ? "0.16.1" : "0.16.2")

config.info("Use Emterpreter => " + use_emterpreter)


//=//// PARSE SCRIPT LOCATION FOR LOADER OPTIONS //////////////////////////=//
//
// The script can read arguments out of the "location", which is the part of
// the URL bar which comes after a ? mark.  So for instance, this would ask
// to load the JS files relative to %replpad-js/ on localhost:
//
//     http://localhost:8000/replpad-js/index.html?local
//

let is_debug = false
let base_dir = null
let me = document.querySelector('script[src$="/load-r3.js"]')

let args = (location.search
    ? location.search.substring(1).split('&')
    : []
)
for (let i = 0; i < args.length; i++) {
    args[i] = decodeURIComponent(args[i])
    if (args[i] == 'debug') {
        is_debug = true
    } else if (args[i] == 'local') {
        base_dir = "./"
    } else if (args[i] == 'remote') {
        base_dir = "https://metaeducation.s3.amazonaws.com/travis-builds/"
    }
}

if (is_debug) {
    let old_alert = window.alert
    window.alert = function(message) {
        config.error(message)
        old_alert(message)
        debugger
    }
}

if (!base_dir) {
    //
    // Default to using the base directory as wherever the %load-r3.js was
    // fetched from.  Today, that is typically on Travis.
    //
    // The directory should have subdirectories %0.16.2/ (for WASM threading)
    // and %0.16.1/ (for emterpreter files).
    //
    // WARNING: for this detection to work, load-r3.js URL MUST CONTAIN '/':
    // USE './load-r3.js' INSTEAD OF 'load-r3.js'"
    //

    base_dir = me.src
    base_dir = base_dir.substring(0, base_dir.indexOf("load-r3.js"))

    if (base_dir == "http://metaeducation.s3.amazonaws.com/travis-builds/") {
        // correct http => https
        base_dir = "https://metaeducation.s3.amazonaws.com/travis-builds/"
    }
}


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

// For hosted builds, this variable is fetched from:
// https://metaeducation.s3.amazonaws.com/travis-builds/${OS_ID}/last-deploy.short-hash
// that contains `${GIT_COMMIT_SHORT}`, see comments in .travis.yml
// If not fetching a particular commit, it must be set to at least ""
//
let git_commit = undefined

let assign_git_commit_promiser = (os_id) => {  // assigns, but no return value
    if (base_dir != "https://metaeducation.s3.amazonaws.com/travis-builds/") {
        git_commit = ""
        return Promise.resolve(undefined)
    }
    return fetch(base_dir + os_id + "/last-deploy.short-hash")
      .then((response) => {

        // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
        if (!response.ok)
            throw Error(response.statusText)  // handled by .catch() below
        return response.text()  // text() returns a "UVString" Promise

      }).then((text) => {

        git_commit = text
        return Promise.resolve(undefined)

      })
}

let lib_suffixes = [
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
    // to letiables on the GUI thread).
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


Module = {  // Note that this is assigning a global
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
        config.info("Module.locateFile() asking for .wasm address of " + s)

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
let dom_content_loaded_promise
if (document.readyState == "loading") {
    dom_content_loaded_promise = new Promise(function (resolve, reject) {
        document.addEventListener('DOMContentLoaded', resolve)
    })
} else {
    // event 'DOMContentLoaded' is gone
    dom_content_loaded_promise = Promise.resolve()
}

let runtime_init_promise = new Promise(function(resolve, reject) {
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
// See notes on short_hash_promiser for why it's a "promiser", not a promise
//
let bytecode_promiser
if (!use_emterpreter)
    bytecode_promiser = () => {
        config.info("Not emterpreted libr3.js, not requesting bytecode")
        return Promise.resolve()
    }
else {
    bytecode_promiser = () => {
        let url = libRebolComponentURL(".bytecode")
        config.info("Emterpreted libr3.js, requesting bytecode from:" + url)

        return fetch(url)
          .then(function(response) {

            // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
            if (!response.ok)
                throw Error(response.statusText)  // handled by .catch() below

            return response.arrayBuffer()  // arrayBuffer() method is a promise

          }).then(function(buffer) {

            Module.emterpreterFile = buffer  // must load before emterpret()
          })
    }
}

let load_rebol_scripts = function(defer) {
    let scripts = document.querySelectorAll("script[type='text/rebol']")
    let promise = Promise.resolve(null)
    for (let i = 0; i < scripts.length; i++) {
        if (scripts[i].defer != defer)
            continue;
        let url = scripts[i].src  // remotely specified via link
        if (url)
            promise = promise.then(function() {
                config.log('fetch()-ing <script src="' + url + '">')
                return fetch(url).then(function(response) {
                    // https://www.tjvantoll.com/2015/09/13/fetch-and-errors/
                    if (!response.ok)
                        throw Error(response.statusText)

                    return response.text()  // returns promise ("USVString")
                  })
                })

        let code = scripts[i].innerText.trim()  // literally in <script> tag
        if (code)
            promise = promise.then(function () {
                return code
            })

        if (code || url)  // promise was augmented to return source code
            promise = promise.then(function (text) {
                config.log("Running <script> code " + code || src)

                // !!! The do { } is necessary here in case the code is a
                // Module or otherwise needs special processing.  Otherwise,
                // `Rebol [Type: Module ...] <your code>` will just evaluate
                // Rebol to an object and throw it away, and evaluate the spec
                // block to itself and throw that away.  The mechanics for
                // recognizing that special pattern are in do.
                //
                reb.Elide("do {" + text + "}")
                config.log("Finished <script> code @ tick " + reb.Tick())
                alert(url+" defer= "+scripts[i].defer)
              })
    }
    return promise
}

return assign_git_commit_promiser(os_id)  // sets git_commit
  .then(bytecode_promiser)  // needs git_commit
  .then(function() {

    load_js_promiser(libRebolComponentURL(".js"))

  }).then(function() {

    config.info('Loading/Running ' + libRebolComponentURL(".js") + '...')
    if (use_emterpreter)
        config.warn("Using Emterpreter is SLOW! Be patient...")

    return runtime_init_promise

  }).then(function() {  // emscripten's onRuntimeInitialized() has no args

    config.info('Executing Rebol boot code...')
    reb.Startup()

    // Scripts have to have an idea of what the "current directory is" when
    // they are running.  If a resource is requested as a FILE! (as opposed
    // to an absolute URL!) it is fetched by path relative to that.  What
    // makes the most sense as the starting "directory" on the web is what's
    // visible in the URL bar.  Then, executing URLs can change the "current"
    // notion to whatever scripts you DO, while they are running.
    //
    // Method chosen for getting the URL dir adapted one that included slash:
    // https://stackoverflow.com/a/16985358
    //
    let url = document.URL
    let base_url
    if (url.charAt(url.length - 1) === '/')
        base_url = url
    else
        base_url = url.slice(0, url.lastIndexOf('/')) + '/'

    // Note: this sets `system/options/path`, so that functions like DO can
    // locate relative FILE!s, e.g. `do %script.reb` knows where to look.
    //
    reb.Elide("change-dir as url!", reb.T(base_url))

    // There is currently no method to dynamically load extensions with
    // r3.js, so the only extensions you can load are those that are picked
    // to be built-in while compiling the lib.  The "JavaScript extension" is
    // essential--it contains JS-NATIVE and JS-AWAITER.
    //
    config.info('Initializing extensions')
    reb.Elide(
        "for-each collation builtin-extensions",
            "[load-extension collation]"
    )
  }).then(()=>load_rebol_scripts(false))
  .then(dom_content_loaded_promise)
  .then(()=>load_rebol_scripts(true))
  .then(()=>{
      let code = me.innerText.trim()
      if (code) eval(code)
  })

//=//// END ANONYMOUS CLOSURE USED AS MODULE //////////////////////////////=//
//
// To help catch cases where `console.log` is used instead of `config.log`,
// we declare a local `console` to force errors.  But we want to be able to
// use the standard console in the default configuration, so we have to pass
// it in so it can be used by another name in the inner scope.
//
// Using bind() just lets us do this by removing a parameter from the 2-arg
// function (and passing `this` as null, which is fine since we don't use it.)

}.bind(null, console)
