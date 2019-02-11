//
//  File: %mod-javascript.c
//  Summary: "Support for calling Javascript from Rebol in Emscripten build"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This module enables the creation of Rebol ACTION!s whose bodies are strings
// of JavaScript code.  To use it, Rebol must be built with emscripten:
//
// http://kripken.github.io/emscripten-site/
//
// Once built, it must be loaded into a host (e.g. a web browser or node.js)
// which provides a JavaScript interpreter.
//
// Key to the implementation being useful is integration with "Promises".
// This deals with the idea that code may not be able to run to completion,
// due to a synchronous dependency on something that must be fulfilled
// asynchronously (like trying to implement INPUT in JavaScript, which has
// to yield to the browser to interact with the DOM).
//
// This means the interpreter state must be able to suspend, ask for the
// information, and wait for an answer.  There are two ways to do this:
//
// 1. Using the PTHREAD emulation of SharedArrayBuffer plus a web worker...so
// that the worker can do an Atomics.wait() on a queued work request, while
// still retaining its state on the stack.
//
// 2. Using the "emterpreter" feature of emscripten, which doesn't run WASM
// code directly--rather, it simulates it in a bytecode.  The bytecode
// interpreter can be suspended while retaining the state of the stack of the
// C program it is implementing.
//
// Currently both approaches are supported, but #1 is far superior and should
// be preferred in any browser that has WASM with PTHREADs.  When this is
// ubiquitous (or maybe earlier) then approach #2 will be dropped.
//
// What the promise does is it returns an integer of a unique memory address
// it allocated to use in a mapping for the [resolve, reject] functions.
// It will trigger those mappings when the promise is fulfilled.  In order to
// come back and do that fulfillment, it either puts the code processing into
// a timer callback (emterpreter) or queues it to a thread (pthreads).
//
// The resolve will be called if it reaches the end of the input and the
// reject if there is a failure.
//

#include "sys-core.h"

#include "tmp-mod-javascript.h"

#include <limits.h> // for UINT_MAX

#include <emscripten.h>

// for the ACT_DETAILS() array of a javascript native

#define IDX_JS_NATIVE_HANDLE \
    IDX_NATIVE_MAX // handle gives hookpoint for GC of table entry

#define IDX_JS_NATIVE_MAX \
    (IDX_JS_NATIVE_HANDLE + 1)


//
//  JavaScript_Native_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.
//
REB_R JavaScript_Native_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    UNUSED(details);

    // Getting a value back from JavaScript via EM_ASM_INT can give back an
    // unsigned int.  There are cases in the emscripten code where this is
    // presumed to be good enough to hold any heap address.  Do a sanity
    // check that we aren't truncating.
    //
    uintptr_t id = cast(uintptr_t, ACT_PARAMLIST(FRM_PHASE(f)));
    assert(id < UINT_MAX);

    uintptr_t r = EM_ASM_INT({
        return RL_Dispatch($0);
    }, id);

    return VAL(cast(void*, r));
}


//
//  export rebpromise-helper: native [
//
//  {Internal routine that helps implement rebPromise() API}
//
//      return: "ID number used in callback to identify this promise"
//          [integer!]
//      args "Variadic feed of arguments to the promise"
//          [<opt> any-value! <...>]
//
//  ]
//
REBNATIVE(rebpromise_helper)
//
// See %make-reb-lib.r for code that produces the `rebPromise(...)` API, which
// translates into a call to `rebUnboxInteger(rebPROMISE_HELPER, ...)`.
// It then wraps that integer into a JavaScript Promise, which ties together
// the integer to a resolve and reject function
//
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_REBPROMISE_HELPER;

    // If we're using a thread model to implement the pausing, then we would
    // have to start executing on that thread here.  The return value model
    // right now is simple and doesn't have a notion for returning either a
    // promise or not, so we always have to return a value that translates to
    // a promise...hence we can't (for instance) do the calculation and notice
    // no asynchronous information was needed.  That is an optimization which
    // could be pursued later.
    //
    // But since that's not what this is doing right now, go ahead and spool
    // the va_list into an array to be executed after a timeout.
    //
    // Currently such spooling is not done except with a frame, and there are
    // a lot of details to get right.  For instance, CELL_FLAG_EVAL_FLIP and
    // all the rest of that.  Plus there may be some binding context
    // information coming from the callsite (?).  So here we do a reuse of
    // the code the GC uses to reify va_lists in frames, which we presume does
    // all the ps and qs.  It's messy, but refactor if it turns out to work.

    REBFRM *f = frame_;
    UNUSED(ARG(args));  // we have the feed from the native's frame directly

    REBDSP dsp_orig = DSP;
    while (NOT_END(f->feed->value))
        Literal_Next_In_Frame(DS_PUSH(), f);

    REBARR *a = Pop_Stack_Values(dsp_orig);
    assert(NOT_SERIES_FLAG(a, MANAGED));  // using array as ID, don't GC it

    EM_ASM_({
        setTimeout(function() {  // evaluate the code w/no other code on GUI
            rebRun(rebU(rebPROMISE_CALLBACK), rebI($0));
        }, 0);
    }, cast(intptr_t, a));

    return Init_Integer(D_OUT, cast(intptr_t, a));
}


//
//  export rebpromise-callback: native [
//
//  {Internal routine that helps implement rebPromise() API}
//
//      return: "Should only be run at topmost GUI stack level--no return"
//          <void>
//      promise-id "The ID of the Promise to do the callback for"
//          [integer!]
//  ]
//
REBNATIVE(rebpromise_callback)
//
// In the emterpreter build, this is the code that rebPromise() defers to run
// until there is no JavaScript above it or after it on the GUI thread stack.
// This makes it safe to use emscripten_sleep_with_yield() inside of it.
//
// Note it cannot be called via a `cwrap` function, only those that have been
// manually wrapped in %make-lib-reb.r without using Emscripten's `cwrap`.
// rebRun() and other variadics fit into this category of manual wrapping.
//
// The problem with using cwraps is that emscripten_sleep_with_yield() sets
// EmterpreterAsync.state to 1 while it is unwinding, and the cwrap() 
// implementation checks the state *after* the call that it is 0...since
// usually, continuing to run would mean running more JavaScript.  We should
// be *sure* this is in an otherwise empty top-level handler (e.g. a timer
// callback for setTimeout())
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_REBPROMISE_CALLBACK;

    intptr_t promise_id = cast(intptr_t, VAL_INT64(ARG(promise_id)));
    REBARR *arr = cast(REBARR*, cast(void*, promise_id));

    // !!! Should probably do a Push_Trap in order to make sure the REJECT can
    // be called.

    // We took off the managed flag in order to avoid GC.  Let's put it back
    // on... the evaluator will lock it.
    //
    // !!! We probably can't unmanage and free it after because it (may?) be
    // legal for references to that array to make it out to the debugger?
    //
    assert(NOT_SERIES_FLAG(arr, MANAGED));
    SET_SERIES_FLAG(arr, MANAGED);

    REBVAL *result = Alloc_Value();
    if (THROWN_FLAG == Eval_Array_At_Mutable_Core(
        Init_Void(result),
        nullptr, // opt_first (null indicates nothing, not nulled cell)
        arr,
        0, // index
        SPECIFIED,
        EVAL_MASK_DEFAULT
            | EVAL_FLAG_TO_END
    )){
        fail (Error_No_Catch_For_Throw(result)); // no need to release result
    }

    if (IS_NULLED(result)) {
        rebRelease(result); // recipient must release if not nullptr
        result = nullptr;
    }

    EM_ASM_({
        RL_Resolve($0, $1); // assumes it can now free the table entry
    }, promise_id, result);

    return Init_Void(D_OUT);
}


//
//  JavaScript_Awaiter_Dispatcher: C
//
// Called when the ACTION! produced by JS-AWAITER is run.  The tricky bit is
// that it doesn't actually return to the caller when the body of the JS code
// is done running...it has to wait for either the `resolve` or `reject`
// parameter functions to get called.
//
REB_R JavaScript_Awaiter_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    UNUSED(details);

    // Getting a value back from JavaScript via EM_ASM_INT can give back an
    // unsigned int.  There are cases in the emscripten code where this is
    // presumed to be good enough to hold any heap address.  Do a sanity
    // check that we aren't truncating.
    //
    uintptr_t id = cast(uintptr_t, ACT_PARAMLIST(FRM_PHASE(f)));
    assert(id < UINT_MAX);

    REBYTE volatile atomic = 0;

    // RL_AsyncDispatch() creates two functions that signal back when called
    // to write the atomic with either a 1 or a 2, indicating completion.
    //
    EM_ASM_({
        RL_AsyncDispatch($0, $1)
    }, id, &atomic);

    while (atomic == 0) {
        if (Eval_Signals & SIG_HALT) {
            //
            // !!! TBD: How to handle halts?  We're spinning here, so the
            // GUI should theoretically have a chance to write some cancel.
            // Is it a reject()?
            //
            Eval_Signals &= ~SIG_HALT; // don't preserve state once observed
        }
        emscripten_sleep_with_yield(50); // no resolve(), no reject() calls...
    }

    // Current limitations disallow the calling of EXPORTED_FUNCTIONS during
    // an emscripten_sleep_with_yield().  This might be something that can
    // be worked around with the PTHREAD worker situation...rebPromise()
    // could spawn a thread, which could block and queue off work to the
    // main thread, but then the main thread could still call rebSpell() or
    // other routines that might be necessary to manipulate handles.
    //
    // We work around this limitation in the emterpreter for the moment by
    // forcing all the asynchronous work to speak in terms of JavaScript types
    // and then resolve() with a function that is run -after- the yield,
    // which can do whatever work it needs to with the API to translate the
    // JavaScript types back into Rebol.

    uintptr_t r = EM_ASM_INT({
        return RL_Await($0)
    }, &atomic);

    REBVAL *result = cast(REBVAL*, r);
    return result;
}


//
// cleanup_js_native: C
//
// GC-able HANDLE! avoids leaking in the table mapping integers (pointers) to
// JavaScript functions.
//
void cleanup_js_native(const REBVAL *v) {
    uintptr_t id = cast(uintptr_t, VAL_HANDLE_POINTER(REBARR*, v));
    assert(id < UINT_MAX);

    EM_ASM_({
        RL_Unregister($0);
    }, id);
}


//
//  Make_JavaScript_Action_Common: C
//
// Shared code for creating JS-NATIVE and JS-PROMISE, since there's a fair
// bit in common.
//
REBACT *Make_JavaScript_Action_Common(
    const REBVAL *spec,
    const REBVAL *source,
    bool is_awaiter
){
    // !!! Should it optimize for empty source in the native case with
    // Voider_Dispatcher()?  Note that empty source for a promise will not
    // be fulfilled...

    REBACT *action = Make_Action(
        Make_Paramlist_Managed_May_Fail(spec, MKF_MASK_NONE),
        is_awaiter
            ? &JavaScript_Awaiter_Dispatcher
            : &JavaScript_Native_Dispatcher,
        nullptr, // no underlying action (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_JS_NATIVE_MAX // details len [source module linkname tcc_state]
    );

    REBARR *details = ACT_DETAILS(action);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), source); // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At_Len(source, -1) // might change
        );
    }

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Unencoded(mo->series, "RL_Register(");

    REBYTE buf[60]; // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(buf, cast(uintptr_t, ACT_PARAMLIST(action)));
    Append_Unencoded_Len(mo->series, s_cast(buf), len);

    Append_Unencoded(mo->series, ", function() {\n"); // would add ID number
    Append_Unencoded(mo->series, "return function"); // !!!! async function?
    if (is_awaiter)
        Append_Unencoded(mo->series, "(resolve, reject)"); // implicit args
    else
        Append_Unencoded(mo->series, "()"); // only has rebArg(...)
    Append_Unencoded(mo->series, " {");

    // By not using `new function` we are able to make this an async function,
    // as well as avoid escaping of string literals.

    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(
        &offset,
        &size,
        source,
        VAL_LEN_AT(source)
    );
    Append_Utf8_Utf8(mo->series, cs_cast(BIN_AT(temp, offset)), size);

    Append_Unencoded(mo->series, "}\n"); // end `function() {`
    Append_Unencoded(mo->series, "}()"); // invoke dummy function
    Append_Unencoded(mo->series, ");"); // end `RL_Register(`

    TERM_SERIES(mo->series);

    emscripten_run_script(cs_cast(BIN_AT(mo->series, mo->start)));

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like rebRun() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    Init_Handle_Managed(
        ARR_AT(details, IDX_JS_NATIVE_HANDLE),
        ACT_PARAMLIST(action),
        0,
        &cleanup_js_native
    );

    SET_ACTION_FLAG(action, IS_NATIVE);
    return action;
}


//
//  export js-native: native [
//
//  {Create ACTION! from JavaScript code}
//
//      return: [action!]
//      spec [block!]
//          {Function specification (similar to the one used by FUNC)}
//      source [text!]
//          {JavaScript code as a text string}
//  ]
//
REBNATIVE(js_native)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_NATIVE;

    REBACT *native = Make_JavaScript_Action_Common(
        ARG(spec),
        ARG(source),
        false // not is_awaiter
    );

    return Init_Action_Unbound(D_OUT, native);
}


//
//  export js-awaiter: native [
//
//  {Create ACTION! from JavaScript code, won't return until resolve() called}
//
//      return: [action!]
//      spec [block!]
//          {Function specification (similar to the one used by FUNC)}
//      source [text!]
//          {JavaScript code as a text string}
//  ]
//
REBNATIVE(js_awaiter)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_AWAITER;

    REBACT *awaiter = Make_JavaScript_Action_Common(
        ARG(spec),
        ARG(source),
        true // is_awaiter
    );

    return Init_Action_Unbound(D_OUT, awaiter);
}


//
//  export init-javascript-extension: native [
//
//  {Initialize the JavaScript Extension}
//
//      return: <void>
//  ]
//
REBNATIVE(init_javascript_extension)
//
// !!! Currently only used to detect that the initialization code in
// %ext-javascript-init.reb actually ran.
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_INIT_JAVASCRIPT_EXTENSION;

    /* printf("JavaScript extension initializing"); */
    return Init_Void(D_OUT);
}
