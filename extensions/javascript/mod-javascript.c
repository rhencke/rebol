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
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %extensions/javascript/README.md
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * This extension expands the RL_rebXXX() API with new entry points.  It
//   was tried to avoid this--doing everything with helper natives.  This
//   would use things like `reb.UnboxInteger("rebpromise-helper", ...)` and
//   build a pure-JS reb.Promise() on top of that.  But in addition to the
//   inefficiency intrinsic to such approaches, reb.UnboxInteger() has to
//   allocate stack for the va_list calling convention.  This disrupts the
//   "sneaky exit and reentry" done by the emterpreter.  All told, adding
//   raw WASM entry points like RL_rebPromise_internal() is more practical,
//   and happens to be faster too.
//
// * Return codes from pthread primitives that can only come from usage errors
//   are not checked (e.g. `pthread_mutex_lock()`).  We only check ones from
//   circumstances like system resource exhaustion (e.g. `pthread_create()`).
//   This tradeoff balances readability.  Example precedent:
//
//   https://www.cs.cmu.edu/afs/cs/academic/class/15492-f07/www/pthreads.html
//
// * If the code block in the EM_ASM() family of functions contains a comma,
//   then wrap the whole code block inside parentheses ().  See the examples
//   which are cited in %em_asm.h
//
// * Emscripten's pthread build thankfully includes MAIN_THREAD_EM_ASM.  It's
//   useful, but can't take care of *all* of our mutex/signaling concerns  The
//   reason is that when you're finished running a JS-AWAITER you want the
//   worker thread to stay blocked even though the code you asked to run has
//   synchronously finished.  The only way around this would be if you could
//   use `await` (you can't...and also it would limit error handling)
//
// * We used to block the main thread while Rebol code for a promise was
//   running on the worker.  But it's rude to lock up the main thread while
//   Rebol is running long operations (JS might want to repaint or do some
//   other handling in parallel) -or- it might want to ask for cancellation.
//   So another way needs to be found.
//

// Older emscripten.h do an #include of <stdio.h>, so %sys-core.h must allow
// it until this: https://github.com/emscripten-core/emscripten/pull/8089
//
#if !defined(DEBUG_STDIO_OK)
    #define DEBUG_STDIO_OK
#endif

#include "sys-core.h"

#include "tmp-mod-javascript.h"

#include <limits.h>  // for UINT_MAX

// Quick source links for emscripten.h and em_asm.h (which it includes):
//
// https://github.com/emscripten-core/emscripten/blob/master/system/include/emscripten/emscripten.h
// https://github.com/emscripten-core/emscripten/blob/master/system/include/emscripten/em_asm.h
//
#include <emscripten.h>

#if defined(USE_EMTERPRETER) == defined(USE_PTHREADS)
    #error "Define one (and only one) of USE_EMTERPRETER or USE_PTHREADS"
#endif

#if defined(USE_PTHREADS)
    #include <pthread.h>  // C11 threads not much better (and less portable)

    static pthread_t PG_Main_Thread;
    static pthread_t PG_Worker_Thread;

    // For why pthread conditions need a mutex:
    // https://stackoverflow.com/q/2763714/

    static pthread_mutex_t PG_Promise_Mutex;
    static pthread_cond_t PG_Promise_Cond;  // when new promise is queued
    static pthread_mutex_t PG_Await_Mutex;
    static pthread_cond_t PG_Await_Cond;  // when JS-AWAITER resolves/rejects

    #define ON_MAIN_THREAD \
        pthread_equal(pthread_self(), PG_Main_Thread)

    inline static void ASSERT_ON_MAIN_THREAD() {  // in a browser, this is GUI
        if (not ON_MAIN_THREAD)
            assert(!"Expected to be on MAIN thread but wasn't");
    }

    inline static void ASSERT_ON_PROMISE_THREAD() {
        if (ON_MAIN_THREAD)
            assert(!"Didn't expect to be on MAIN thread but was");
    }
#else
    #define ON_MAIN_THREAD                  true
    #define ASSERT_ON_PROMISE_THREAD()      NOOP
    #define ASSERT_ON_MAIN_THREAD()         NOOP
#endif


//=//// DEBUG_JAVASCRIPT_EXTENSION TOOLS //////////////////////////////////=//
//
// Ren-C has a very aggressive debug build.  Turning on all the debugging
// means a prohibitive experience in emscripten--not just in size and speed of
// the build products, but the compilation can wind up taking a long time--or
// not succeeding at all).
//
// So most of the system is built with NDEBUG, and no debugging is built
// in for the emscripten build.  The hope is that the core is tested elsewhere
// (or if a bug is encountered in the interpreter under emscripten, it will
// be reproduced and can be debugged in a non-JavaScript build).
//
// However, getting some amount of feedback in the console is essential to
// debugging the JavaScript extension itself.  These are some interim hacks
// for doing that until better ideas come along.

#ifdef DEBUG_JAVASCRIPT_SILENT_TRACE

    // Trace output can influence the behavior of the system so that race
    // conditions or other things don't manifest.  This is tricky.  If this
    // happens we can add to the silent trace buffer.
    //
    static char PG_Silent_Trace_Buf[64000] = "";

    EXTERN_C intptr_t RL_rebGetSilentTrace_internal(void) {
      { return cast(intptr_t, cast(void*, PG_Silent_Trace_Buf)); }
#endif

#ifdef DEBUG_JAVASCRIPT_EXTENSION
    #undef assert  // if it was defined (most emscripten builds are NDEBUG)
    #define assert(expr) \
        do { if (!(expr)) { \
            printf("%s:%d - assert(%s)\n", __FILE__, __LINE__, #expr); \
            exit(0); \
        } } while (0)

    static bool PG_JS_Trace = false;  // Turned on/off with JS-TRACE native

    #define TRACE(...)  /* variadic, but emscripten is at least C99! :-) */ \
        do { if (PG_JS_Trace) { \
            printf("@%ld: ", cast(long, TG_Tick));  /* tick count prefix */ \
            printf("%c ", ON_MAIN_THREAD ? 'M' : 'P');  /* thread */ \
            printf(__VA_ARGS__); \
            printf("\n");  /* console.log() won't show up until newline */ \
            fflush(stdout);  /* just to be safe */ \
        } } while (0)

    // TRASH_POINTER_IF_DEBUG() is defined in release builds as a no-op, but
    // it's kind of complicated.  For the purposes in this file these END
    // macros work just as well and don't collide.

    #define ENDIFY_POINTER_IF_DEBUG(p) \
        p = m_cast(REBVAL*, END_NODE)

    #define IS_POINTER_END_DEBUG(p) \
        (p == m_cast(REBVAL*, END_NODE))

    // One of the best pieces of information to follow for a TRACE() is what
    // the EM_ASM() calls.  So printing the JavaScript sent to execute is
    // very helpful.  But it's not possible to "hook" EM_ASM() in terms of
    // its previous definition:
    //
    // https://stackoverflow.com/q/3085071/
    //
    // Fortunately the definitions for EM_ASM() are pretty simple, so writing
    // them again is fine...just needs to change if emscripten.h does.
    // (Note that EM_ASM_INT would require changes to TRACE() as implemented)
    //
    #undef EM_ASM
    #define EM_ASM(code, ...) \
        TRACE("EM_ASM(%s)", #code); \
        ((void)emscripten_asm_const_int(#code _EM_ASM_PREP_ARGS(__VA_ARGS__)))
#else
    // assert() is defined as a noop in release builds already

    #define TRACE(...)                      NOOP
    #define ENDIFY_POINTER_IF_DEBUG(p)      NOOP
    #define IS_POINTER_END_DEBUG(p)         NOOP
#endif


//=//// HEAP ADDRESS ABSTRACTION //////////////////////////////////////////=//
//
// Generally speaking, C exchanges integers with JavaScript.  These integers
// (e.g. the ones that come back from EM_ASM_INT) are typed as `unsigned int`.
// That's unfortunately not a `uintptr_t`...which would be a type that by
// definition can hold any pointer.  But there are cases in the emscripten
// code where this is presumed to be good enough to hold any heap address.
//
// Track the places that make this assumption with `heapaddr_t`, and sanity
// check that we aren't truncating any C pointers in the conversions.
//
// Note heap addresses can be used as ID numbers in JavaScript for mapping
// C entities to JavaScript objects that cannot be referred to directly.
// Tables referring to them must be updated when the related pointer is
// freed, as the pointer may get reused.

typedef unsigned int heapaddr_t;

inline static heapaddr_t Heapaddr_From_Pointer(void *p) {
    intptr_t i = cast(intptr_t, cast(void*, p));
    assert(i < UINT_MAX);
    return i;
}

inline static void* Pointer_From_Heapaddr(heapaddr_t addr)
  { return cast(void*, cast(intptr_t, addr)); }


//=//// FRAME ID AND THROWING /////////////////////////////////////////////=//
//
// We go ahead and use the REBCTX* instead of the raw REBFRM* to act as the
// unique pointer to identify a frame.  That's because if the JavaScript code
// throws and that throw needs to make it to a promise higher up the stack, it
// uses that pointer as an ID in a mapping table (on the main thread) to
// associate the call with the JavaScript object it threw.
//
// !!! This aspect is overkill for something that can only happen once on
// the stack at a time.  Review.
//
// !!! Future designs may translate that object into Rebol so it could
// be caught by Rebol, but for now we assume a throw originating from
// JavaScript code may only be caught by JavaScript code.
//

inline static heapaddr_t Frame_Id_For_Frame_May_Outlive_Call(REBFRM* f) {
    REBCTX *frame_ctx = Context_For_Frame_May_Manage(f);
    return Heapaddr_From_Pointer(frame_ctx);
}

static REBCTX *js_throw_nocatch_converter(REBVAL *thrown)
{
    // If this is a pthread build and we're not on the main thread, we have
    // to do maneuvers to get the information off the GUI thread and to
    // remove the thrown object from the table.  First test the concept.

    assert(IS_HANDLE(VAL_THROWN_LABEL(thrown)));
    CATCH_THROWN(thrown, thrown);
    assert(IS_FRAME(thrown));

    heapaddr_t frame_id = Heapaddr_From_Pointer(VAL_NODE(thrown));

    heapaddr_t error_addr = MAIN_THREAD_EM_ASM_INT(
        { return reb.GetNativeError_internal($0) },
        frame_id  // => $0
    );

    REBVAL *error = VAL(Pointer_From_Heapaddr(error_addr));
    return VAL_CONTEXT(error);
}

inline static REB_R Init_Throw_For_Frame_Id(REBVAL *out, heapaddr_t frame_id)
{
    // Note that once the throw is performed, the context will be expired.
    // VAL_CONTEXT() will fail, so VAL_NODE() must be used to extract the
    // heap address.
    //
    REBCTX *frame_ctx = CTX(Pointer_From_Heapaddr(frame_id));
    REBVAL *frame = CTX_ARCHETYPE(frame_ctx);

    // Use NOCATCH_FUNC trick in the throw machinery, so that we can bubble
    // the throw up and see if we find a rebPromise() which is equipped to
    // unpack a JavaScript object.  If not, the function converts to a
    // Rebol ERROR! failure.
    //
    DECLARE_LOCAL (handle);
    Init_Handle_Cfunc(handle, cast(CFUNC*, &js_throw_nocatch_converter));

    return Init_Thrown_With_Label(out, frame, handle);
}


//=//// JS-NATIVE PER-ACTION! DETAILS /////////////////////////////////////=//
//
// All Rebol ACTION!s that claim to be natives have to provide a BODY field
// for source, and an ANY-CONTEXT! that indicates where any API calls will
// be bound while that native is on the stack.  For now, if you're writing
// any JavaScript native it will presume binding in the user context.
//
// (A refinement could be added to control this, e.g. JS-NATIVE/CONTEXT.
// But generally the caller of the API can override with their own binding.)
//
// For the JS-native-specific information, it uses a HANDLE!...but only to
// get the GC hook a handle provides.  When a JavaScript native is GC'd, it
// calls into JavaScript to remove the mapping from integer to function that
// was put in that table at the time of creation (the native_id).
//

inline static heapaddr_t Native_Id_For_Action(REBACT *act)
  { return Heapaddr_From_Pointer(ACT_PARAMLIST(act)); }

#define IDX_JS_NATIVE_HANDLE \
    IDX_NATIVE_MAX  // handle gives hookpoint for GC of table entry

#define IDX_JS_NATIVE_IS_AWAITER \
    (IDX_NATIVE_MAX + 1)  // LOGIC! of if this is an awaiter or not

#define IDX_JS_NATIVE_MAX \
    (IDX_JS_NATIVE_IS_AWAITER + 1)

REB_R JavaScript_Dispatcher(REBFRM *f);

static void cleanup_js_native(const REBVAL *v) {
    REBARR *paramlist = ARR(VAL_HANDLE_POINTER(REBARR*, v));
    heapaddr_t native_id = Native_Id_For_Action(ACT(paramlist));
    assert(native_id < UINT_MAX);
    EM_ASM(
        { reb.UnregisterId_internal($0); },  // don't leak map[int->JS funcs]
        native_id  // => $0
    );
}


//=//// GLOBAL PROMISE STATE //////////////////////////////////////////////=//
//
// Several promises can be requested sequentially, and so they queue up in
// a linked list.  If Rebol were multithreaded, we would be able to start
// those threads and run them while the MAIN were still going...but since it
// is not, we have to wait until the MAIN is idle and isn't making any calls
// into libRebol.
//
// See %extensions/javascript/README.md for a discussion of the emterpreter
// option vs. the PTHREAD option.
//

enum Reb_Promise_State {
    PROMISE_STATE_QUEUEING,
    PROMISE_STATE_RUNNING,
    PROMISE_STATE_AWAITING,
    PROMISE_STATE_RESOLVED,
    PROMISE_STATE_REJECTED
};

struct Reb_Promise_Info {
    enum Reb_Promise_State state;
    heapaddr_t promise_id;

    struct Reb_Promise_Info *next;
};

static struct Reb_Promise_Info *PG_Promises;  // Singly-linked list


enum Reb_Native_State {
    NATIVE_STATE_NONE,
    NATIVE_STATE_RUNNING,
    NATIVE_STATE_RESOLVED,
    NATIVE_STATE_REJECTED
};

// Information cannot be exchanged between the worker thread and the main
// thread via JavaScript values, so they are proxied between threads as
// heap pointers via these globals.
//
static REBVAL *PG_Native_Result;
static enum Reb_Native_State PG_Native_State;


// This returns an integer of a unique memory address it allocated to use in
// a mapping for the [resolve, reject] functions.  We will trigger those
// mappings when the promise is fulfilled.  In order to come back and do that
// fulfillment, it either puts the code processing into a timer callback
// (emterpreter) or queues it to a thread (pthreads).
//
// The resolve will be called if it reaches the end of the input and the
// reject if there is a failure.
//
// Note: See %make-reb-lib.r for code that produces the `rebPromise(...)` API,
// which ties the returned integer into the resolve and reject branches of an
// actual JavaScript ES6 Promise.
//
EXTERN_C intptr_t RL_rebPromise(REBFLGS flags, void *p, va_list *vaptr)
{
    TRACE("rebPromise() called");
    ASSERT_ON_MAIN_THREAD();

    // If we're asked to run `rebPromise("input")` from the MAIN thread, there
    // is no way of that being fulfilled synchronously.  But could if you were
    // doing something like `rebPromise("1 + 2")`.  Speculatively running
    // and then yielding only on asynchronous requests would be *technically*
    // possible in the pthread model, but would require each API entry point
    // to take an interpreter lock.  The emterpreter is incapable of doing it
    // (it would be stuck in a JS stack it can't sleep_with_yield() from).
    //
    // But there's also an issue that if we allow a thread to run now, then we
    // would have to block the MAIN thread from running.  And while the MAIN
    // was blocked we might actually fulfill the promise in question.  But
    // then this would need a protocol for returning already fulfilled
    // promises--which becomes a complex management exercise of when the
    // table entry is freed for the promise.
    //
    // To keep the contract simple (and not having a wildly different version
    // for the emterpreter vs. not), we don't execute anything now.  Instead
    // we spool the request into an array.  Then we use `setTimeout()` to ask
    // to execute that array in a callback at the top level.  This permits
    // an emterpreter sleep_with_yield(), or running a thread that can take
    // for granted the resolve() function created on return from this helper
    // already exists.

    DECLARE_VA_FEED (feed, p, vaptr, flags);

    REBDSP dsp_orig = DSP;
    while (NOT_END(feed->value)) {
        Derelativize(DS_PUSH(), feed->value, feed->specifier);
        SET_CELL_FLAG(DS_TOP, UNEVALUATED);
        Fetch_Next_In_Feed(feed, false);
    }
    // Note: exhausting feed should take care of the va_end()

    REBARR *code = Pop_Stack_Values(dsp_orig);
    assert(NOT_SERIES_FLAG(code, MANAGED));  // using array as ID, don't GC it

    // We singly link the promises such that they will be executed backwards.
    // What's good about that is that it will help people realize that over
    // the long run, there's no ordering guarantee of promises (e.g. if they
    // were running on individual threads).

    struct Reb_Promise_Info *info = ALLOC(struct Reb_Promise_Info);
    info->state = PROMISE_STATE_QUEUEING;
    info->promise_id = cast(intptr_t, code);
    info->next = PG_Promises;
    PG_Promises = info;

  #ifdef USE_EMTERPRETER
    EM_ASM(
        { setTimeout(function() { _RL_rebIdle_internal(); }, 0); }
    );  // note `_RL` (leading underscore means no cwrap)
  #else
    pthread_mutex_lock(&PG_Promise_Mutex);
    pthread_cond_signal(&PG_Promise_Cond);
    pthread_mutex_unlock(&PG_Promise_Mutex);

    // Note: Because the promise resolves via MAIN_THREAD_EM_ASM, it shouldn't
    // be possible for resolution to happen before the promise is wrapped up.
  #endif

    return info->promise_id;
}

struct ArrayAndBool {
    REBARR *code;
    bool failed;
};

// Function passed to rebRescue() so code can be run but trap errors safely.
//
REBVAL *Run_Array_Dangerous(void *opaque) {
    struct ArrayAndBool *x = cast(struct ArrayAndBool*, opaque);

    x->failed = true;  // assume it failed if the end was not reached

    REBVAL *result = Alloc_Value();
    if (Do_At_Mutable_Throws(result, x->code, 0, SPECIFIED)) {
        if (IS_HANDLE(VAL_THROWN_LABEL(result))) {
            CATCH_THROWN(result, result);
            assert(IS_FRAME(result));
            return result;
        }

        fail (Error_No_Catch_For_Throw(result));
    }

    x->failed = false;  // Since end was reached, it did not fail...
    return result;
}


void RunPromise(void)
{
    TRACE("RunPromise() called");

    struct Reb_Promise_Info *info = PG_Promises;
    assert(info->state == PROMISE_STATE_QUEUEING);
    info->state = PROMISE_STATE_RUNNING;

    REBARR *code = ARR(Pointer_From_Heapaddr(info->promise_id));
    assert(NOT_SERIES_FLAG(code, MANAGED));  // took off so it didn't GC
    SET_SERIES_FLAG(code, MANAGED);  // but need it back on to execute it

    // We run the code using rebRescue() so that if there are errors, we
    // will be able to trap them.  the difference between `throw()`
    // and `reject()` in JS is subtle.
    //
    // https://stackoverflow.com/q/33445415/

    struct ArrayAndBool x;  // bool needed to know if it failed
    x.code = code;
    REBVAL *result = rebRescue(&Run_Array_Dangerous, &x);
    assert(not result or not IS_NULLED(result));  // NULL is nullptr in API

    if (info->state == PROMISE_STATE_REJECTED) {
        assert(IS_FRAME(result));
        TRACE("RunPromise() => promise is rejecting due to...something (?)");

        // Note: Expired, can't use VAL_CONTEXT
        //
        assert(IS_FRAME(result));
        REBNOD *frame_ctx = VAL_NODE(result);
        heapaddr_t throw_id = Heapaddr_From_Pointer(frame_ctx);

        MAIN_THREAD_EM_ASM(
            { reb.RejectPromise_internal($0, $1); },
            info->promise_id,  // => $0 (table entry will be freed)
            throw_id  // => $1 (table entry will be freed)
        );
    }
    else {
        assert(info->state == PROMISE_STATE_RUNNING);

        if (x.failed) {
            //
            // Note this could be an uncaught throw error, raised by the
            // Run_Array_Dangerous() itself...or a failure rebRescue()
            // caught...
            //
            assert(IS_ERROR(result));
            info->state = PROMISE_STATE_REJECTED;
            TRACE("RunPromise() => promise is rejecting due to error");
        }
        else {
            info->state = PROMISE_STATE_RESOLVED;
            TRACE("RunPromise() => promise is resolving");

            MAIN_THREAD_EM_ASM(
                { reb.ResolvePromise_internal($0, $1); },
                info->promise_id,  // => $0 (table entry will be freed)
                result  // => $1 (recipient takes over handle)
            );
        }
    }

    rebRelease(result);

    assert(PG_Promises == info);
    PG_Promises = info->next;
    FREE(struct Reb_Promise_Info, info);
}


#if defined(USE_PTHREADS)
    //
    // Worker pthread that loops, picks up promise work items, and runs the
    // associated array of code.
    //
    void *promise_worker(void *vargp) {
        assert(IS_POINTER_END_DEBUG(vargp));  // unused (reads PG_Promises)

        ASSERT_ON_PROMISE_THREAD();

        // This loop should have a signal to exit cleanly and shut down the
        // worker thread: https://forum.rebol.info/t/960
        //
        while (true) {
            TRACE("promise_worker() => waiting on promise request");
            pthread_mutex_lock(&PG_Promise_Mutex);
            pthread_cond_wait(&PG_Promise_Cond, &PG_Promise_Mutex);
            pthread_mutex_unlock(&PG_Promise_Mutex);
            TRACE("promise_worker() => got signal to start running promise");

            RunPromise();  // should be ready to go if we're awoken here
        }
    }
#endif

#if defined(USE_EMTERPRETER)
    //
    // In the emterpreter build, rebPromise() defers to run until there is no
    // JavaScript above it or after it on the MAIN thread stack.
    //
    // Inside this call, emscripten_sleep_with_yield() can sneakily make us
    // fall through to the main loop.  We don't notice it here--it's invisible
    // to the C code being yielded.  -BUT- the JS callsite for rebIdle() would
    // notice, as it would seem rebIdle() had finished...when really what's
    // happening is that the bytecode interpreter is putting it into suspended
    // animation--which it will bring it out of with a setTimeout.
    //
    // (This is why there shouldn't be any meaningful JS on the stack above
    // this besides the rebIdle() call itself.)
    //
    EXTERN_C void RL_rebIdle_internal(void)  // NO user JS code on stack!
    {
        TRACE("rebIdle() => begin emterpreting promise code");
        RunPromise();
        TRACE("rebIdle() => finished emterpreting promise code");
    }
#endif


// The protocol for JavaScript returning Rebol API values to Rebol is to do
// so with functions that either "resolve" (succeed) or "reject" (e.g. fail).
// Even non-async functions use the callbacks, so that they can signal a
// failure bubbling up out of them as distinct from success.
//
// Those callbacks always happen on the main thread.  But the code that wants
// the result may be Rebol running on the worker, or yielded emterpreter code
// that can't actually process the value yet.  So the values are stored in
// a table associated with the call frame's ID.  This pulls that out into the
// PG_Native_Result variable.
//
void Sync_Native_Result(heapaddr_t frame_id) {
    ASSERT_ON_MAIN_THREAD();

    heapaddr_t result_addr = EM_ASM_INT(
        { return reb.GetNativeResult_internal($0) },
        frame_id  // => $0
    );

    assert(IS_POINTER_END_DEBUG(PG_Native_Result));
    PG_Native_Result = VAL(Pointer_From_Heapaddr(result_addr));
}


// This is rebSignalResolveNative() and not rebResolveNative() which passes in
// a value to resolve with, because the emterpreter build can't really pass a
// REBVAL*.   All the APIs it would need to make REBVAL* are unavailable.  So
// it instead pokes a JavaScript function where it can be found when no longer
// in emscripten_sleep_with_yield().
//
// The pthreads build *could* take a value and poke it into the promise info.
// But it's not worth it to wire up two different protocols on the JavaScript
// side.  It should be rethought if someday the emterpreter version is axed.
//
EXTERN_C void RL_rebSignalResolveNative_internal(intptr_t frame_id) {
    ASSERT_ON_MAIN_THREAD();
    TRACE("reb.SignalResolveNative_internal()");

    struct Reb_Promise_Info *info = PG_Promises;

  #if defined(USE_PTHREADS)
    if (info and info->state == PROMISE_STATE_AWAITING)
        pthread_mutex_lock(&PG_Await_Mutex);
  #endif

    assert(PG_Native_State == NATIVE_STATE_RUNNING);
    PG_Native_State = NATIVE_STATE_RESOLVED;

  #if defined(USE_PTHREADS)
    Sync_Native_Result(frame_id);  // must get now if worker is to receive it

    if (info and info->state == PROMISE_STATE_AWAITING) {
        pthread_cond_signal(&PG_Await_Cond);  // no effect if nothing waiting
        pthread_mutex_unlock(&PG_Await_Mutex);
    }
  #endif
}


// See notes on rebSignalResolveNative()
//
EXTERN_C void RL_rebSignalRejectNative_internal(intptr_t frame_id) {
    ASSERT_ON_MAIN_THREAD();
    TRACE("reb.SignalRejectNative_internal()");

    struct Reb_Promise_Info *info = PG_Promises;

  #if defined(USE_PTHREADS)
    if (info and info->state == PROMISE_STATE_AWAITING)
        pthread_mutex_lock(&PG_Await_Mutex);
  #endif

    assert(PG_Native_State == NATIVE_STATE_RUNNING);
    PG_Native_State = NATIVE_STATE_REJECTED;

  #if defined(USE_PTHREADS)
    //
    // This signal is happening during the .catch() clause of the internal
    // routine that runs natives.  But it happens after it is no longer
    // on the stack, e.g. 
    //
    //     async function js_awaiter_impl() { throw 1020; }
    //     function js_awaiter_invoker() {
    //         js_awaiter_impl().catch(function() {
    //              console.log("prints second")  // we're here now
    //         })
    //         console.log("prints first")  // fell through to GUI pump
    //     }
    //
    // So the js_awaiter_invoker() is not on the stack, this is an async
    // resolution even if the throw was called directly like that.
    //
    // In the long term it may be possible for Rebol constructs like
    // TRAP or CATCH to intercept a JavaScript-thrown error.  If they
    // did they may ask for more work to be done on the GUI so it would
    // need to be in idle for that (otherwise the next thing it ran
    // could always be assumed as the result to the await).
    //
    // But if the Rebol construct could catch a JS throw, it would need
    // to convert it somehow to a Rebol value.  That conversion would
    // have to be done right now--or there'd have to be some specific
    // protocol for coming back and requesting it.
    //
    // But what we have to do is unblock the JS-AWAITER that's running
    // with a throw so it can finish.  We do not want to do the promise
    // rejection until it is.  We make that thrown value the frame so
    // we can get the ID back out of it (and so it doesn't GC, so the
    // lifetime lasts long enough to not conflate IDs in the table).
    //
    // Note: We don't want to fall through to the main thread's message
    // pump so long as any code is running on the worker that's using Rebol
    // features.  A stray setTimeout() message might get processed while
    // the R_THROW is being unwound, and use a Rebol API which would
    // be contentious with running code on another thread.  Block, and
    // it should be unblocked to let the catch() clause run.
    //
    // We *could* do mutex management here and finish up the signal
    // sequence.  But we can't on the emterpreted build, because it has
    // to unwind that asm.js stack safely, so we could only call the
    // reject here for pthread.  Pipe everything through idle so both
    // emterpreter and not run the reject on GUI from the same stack.

    // * The JavaScript was running on the GUI thread
    // * What is raised to JavaScript is always a JavaScript error, even if
    //   it is a proxy error for something that happened in a Rebol call.
    // * We leave the error in the table.
    //
    /* Sync_Native_Result(frame_id); */

    if (info and info->state == PROMISE_STATE_AWAITING) {
        pthread_cond_signal(&PG_Await_Cond);  // no effect if nothing waiting
        pthread_mutex_unlock(&PG_Await_Mutex);
    }
  #endif
}

#ifdef USE_PTHREADS
    //
    // When workers ask to synchronously run a JS-AWAITER on the main thread,
    // there is a time window left open between the completion of the function
    // and when the worker receives control back.  This makes a race condition
    // for any resolve() or reject() signals which might happen between when
    // the main execution finishes and when the worker enters a wait state
    // for the result.  So before the blocking call to main returns control,
    // we slip in a lock of a mutex to prevent a signal being sent before the
    // worker is ready for it.
    //
    EXTERN_C void RL_rebTakeAwaitLock_internal(intptr_t frame_id)
    {
        TRACE("reb.TakeAwaitLock_internal()");
        pthread_mutex_lock(&PG_Await_Mutex);
    }
#endif


//
//  JavaScript_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.  The tricky bit is
// that it doesn't actually return to the caller when the body of the JS code
// is done running...it has to wait for either the `resolve` or `reject`
// parameter functions to get called.
//
// An AWAITER can only be called inside a rebPromise().  And it needs its
// body to run on the MAIN thread.
//
REB_R JavaScript_Dispatcher(REBFRM *f)
{
    heapaddr_t native_id = Native_Id_For_Action(FRM_PHASE(f));
    heapaddr_t frame_id = Frame_Id_For_Frame_May_Outlive_Call(f);

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    bool is_awaiter = VAL_LOGIC(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER));

    TRACE("JavaScript_Dispatcher(%s)", Frame_Label_Or_Anonymous_UTF8(f));

    struct Reb_Promise_Info *info = PG_Promises;
    if (is_awaiter) {
        if (info == nullptr)
            fail ("JavaScript /AWAITER can only be called from rebPromise()");
        if (info->state != PROMISE_STATE_RUNNING)
            fail ("Cannot call JavaScript /AWAITER during another await");
    }
    else
        assert(not info or info->state == PROMISE_STATE_RUNNING);

    if (PG_Native_State != NATIVE_STATE_NONE)
        assert(!"Cannot call JS-NATIVE during JS-NATIVE at this time");

    assert(IS_POINTER_END_DEBUG(PG_Native_Result));
    PG_Native_State = NATIVE_STATE_RUNNING;

    // Whether it's an awaiter or not (e.g. whether it has an `async` JS
    // function as the body), the same interface is used to call the function.
    // It will communicate whether an error happened or not through the
    // `rebSignalResolveNative()` or `rebSignalRejectNative()` either way,
    // and the results are fetched with the same mechanic.

  #if defined(USE_EMTERPRETER)  // on MAIN thread (by definition)

    EM_ASM(
        { reb.RunNative_internal($0, $1) },
        native_id,  // => $0
        frame_id  // => $1
    );

    // We don't know exactly what MAIN event is going to trigger and cause a
    // resolve() to happen.  It could be a timer, it could be a fetch(),
    // it could be anything.  The emterpreted build doesn't really have a
    // choice other than to poll...there's nothing like pthread wait
    // conditions available.  We wait at least 50msec (probably more, as
    // we don't control how long the MAIN will be running whatever it does).
    //
    TRACE("JavaScript_Dispatcher() => begin sleep_with_yield() loop");
    while (PG_Native_State == NATIVE_STATE_RUNNING) {  // !!! volatile?
        if (Eval_Signals & SIG_HALT) {
            //
            // !!! TBD: How to handle halts?  We're spinning here, so the
            // MAIN should theoretically have a chance to write some cancel.
            // Is it a reject()?
            //
            Eval_Signals &= ~SIG_HALT; // don't preserve state once observed
        }
        emscripten_sleep_with_yield(50); // no resolve(), no reject() calls...
    }
    TRACE("JavaScript_Dispatcher() => end sleep_with_yield() loop");

    Sync_Native_Result(frame_id);
  #else
    // If we're already on the MAIN thread, then we're just calling a JS
    // service routine with no need to yield.
    //
    if (ON_MAIN_THREAD) {
        //
        // !!! This assertion didn't seem to take into account the case where
        // you call an awaiter from within a function that's part of a
        // resolve callback, e.g.
        //
        //     x: js-awaiter [] {
        //         return reb.Promise((resolve, reject) => {
        //             resolve(() => { reb.Elide("print {Hi}"); })
        //         })
        //     }
        //
        // Since PRINT has an awaiter character, it may actually be run
        // direct from the main thread.  This should be able to work :-/ but
        // due to the resolve not having been run yet there's still an
        // awaiter in-flight, so it has problems.  Review.
        //
        assert(not is_awaiter);

        EM_ASM(
            { reb.RunNative_internal($0, $1) },
            native_id,  // => $0
            frame_id  // => $1
        );

        // Because we were on the main thread we know it's not an awaiter,
        // and hence it must have been resolved while the body was run.
        // (We wouldn't be able to wait for an asynchronous signal on the
        // GUI thread if we blocked here!  This is why reb.Promise() exists!)
    }
    else {
        // We are not using the emterpreter, so we have to block our return
        // on a condition, while signaling the MAIN that it can go ahead and
        // run.  The MAIN has to actually run the JS code.

        info->state = PROMISE_STATE_AWAITING;

        MAIN_THREAD_EM_ASM(  // blocking call
            {
                reb.RunNative_internal($0, $1);  // `;` is necessary here 
                _RL_rebTakeAwaitLock_internal();
            },
            native_id,  // => $0
            frame_id  // => $1
        );

        // While there may have been a resolve or reject during the execution,
        // we guarantee that between then and now there hasn't been one that
        // the signal for could be missed...see rebTakeWorkerLock_internal().

        if (PG_Native_State == NATIVE_STATE_RUNNING) {  // no result...*yet*
            TRACE("JavaScript_Dispatcher() => suspending for native result");
            pthread_cond_wait(&PG_Await_Cond, &PG_Await_Mutex);
            TRACE("JavaScript_Dispatcher() => native result was signaled");
        }
        else {
            assert(
                PG_Native_State == NATIVE_STATE_REJECTED
                or PG_Native_State == NATIVE_STATE_RESOLVED
            );
            TRACE("JavaScript_Dispatcher() => function result during body");
        }

        info->state = PROMISE_STATE_RUNNING;
        pthread_mutex_unlock(&PG_Await_Mutex);
    }
  #endif

    // See notes on frame_id above for how the context's heap address is used
    // to identify the thrown JavaScript object in a mapping table, so that
    // when this throw reaches the top it can be supplied to the caller.
    // (We don't want to unsafely EM_ASM() throw from within a Rebol call in
    // a way that breaks asm.js execution and bypasses the cleanup needed)
    // 
    if (PG_Native_State == NATIVE_STATE_REJECTED) {

        // !!! Testing at the moment to see if a throw can at least not cause
        // a crash (?)  The throw currently goes up and gets caught by
        // someone, can we see what happens?

        PG_Native_State = NATIVE_STATE_NONE;
        return Init_Throw_For_Frame_Id(f->out, frame_id);
    }

    assert(not IS_POINTER_END_DEBUG(PG_Native_Result));
    if (PG_Native_Result == nullptr)
        Init_Nulled(f->out);
    else {
        assert(not IS_NULLED(PG_Native_Result));  // API uses nullptr only
        Move_Value(f->out, PG_Native_Result);
        rebRelease(PG_Native_Result);
    }
    ENDIFY_POINTER_IF_DEBUG(PG_Native_Result);

    assert(PG_Native_State == NATIVE_STATE_RESOLVED);
    PG_Native_State = NATIVE_STATE_NONE;
    return f->out;
}


// The table mapping IDs to JavaScript objects only exists on the main thread.
// See notes at callsite about why MAIN_THREAD_EM_ASM() can't run "scripts".
// Thus the JS-NATIVE generator calls this as a service routine that it can
// ask to be executed by proxy to the main thread.
//
EXTERN_C void RL_rebRegisterNative_internal(intptr_t native_id) {
    ASSERT_ON_MAIN_THREAD();

    REBACT *act = ACT(Pointer_From_Heapaddr(native_id));

    REBARR *details = ACT_DETAILS(act);
    RELVAL *source = ARR_AT(details, IDX_NATIVE_BODY);
    bool is_awaiter = VAL_LOGIC(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER));

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Ascii(mo->series, "let f = ");  // variable we store function in

    // By not using `new function` we avoid escaping of the string literal.
    // We also have the option of making it an async function in the future
    // if we wanted to...which would allow `await` in the body.

    // A JS-AWAITER can only be triggered from Rebol on the worker thread as
    // part of a rebPromise().  Making it an async function means it will
    // return an ES6 Promise, and allows use of the AWAIT JavaScript feature
    // inside the body:
    //
    // https://javascript.info/async-await
    //
    // Using plain return inside an async function returns a fulfilled promise
    // while using AWAIT causes the execution to pause and return a pending
    // promise.  When that promise is fulfilled it will jump back in and
    // pick up code on the line after that AWAIT.
    //
    if (is_awaiter)
        Append_Ascii(mo->series, "async ");

    // We do not try to auto-translate the Rebol arguments into JS args.  It
    // would make calling it more complex, and introduce several issues of
    // mapping Rebol names to legal JavaScript identifiers.  reb.Arg() or
    // reb.ArgR() must be used to access the arguments out of the frame.
    //
    Append_Ascii(mo->series, "function () {");
    Append_String(mo->series, source, VAL_LEN_AT(source));
    Append_Ascii(mo->series, "};\n");  // end `function() {`

    if (is_awaiter)
        Append_Ascii(mo->series, "f.is_awaiter = true;\n");
    else
        Append_Ascii(mo->series, "f.is_awaiter = false;\n");

    REBYTE id_buf[60];  // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(id_buf, native_id);

    // Rebol cannot hold onto JavaScript objects directly, so there has to be
    // a table mapping some numeric ID (that we *can* hold onto) to the
    // corresponding JS function entity.
    //
    Append_Ascii(mo->series, "reb.RegisterId_internal(");
    Append_Ascii_Len(mo->series, s_cast(id_buf), len);
    Append_Ascii(mo->series, ", f);\n");

    // The javascript code for registering the function body is now the last
    // thing in the mold buffer.  Get a pointer to it.
    //
    TERM_SERIES(SER(mo->series));
    const char *js = cs_cast(BIN_AT(SER(mo->series), mo->offset));

    TRACE("Registering native_id %ld", cast(long, native_id));
    emscripten_run_script(js);

    Drop_Mold(mo);
}


//
//  export js-native: native [
//
//  {Create ACTION! from textual JavaScript code, works only on main thread}
//
//      return: [action!]
//      spec "Function specification (similar to the one used by FUNCTION)"
//          [block!]
//      source "JavaScript code as a text string" [text!]
//      /awaiter "Uses async JS function, invocation will implicitly `await`"
//  ]
//
REBNATIVE(js_native)
//
// Note: specialized as JS-AWAITER in %ext-javascript-init.reb
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_NATIVE;

    REBVAL *spec = ARG(spec);
    REBVAL *source = ARG(source);

    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(spec, MKF_MASK_NONE);
    REBACT *native = Make_Action(
        paramlist,
        &JavaScript_Dispatcher,
        nullptr,  // no underlying action (use paramlist)
        nullptr,  // no specialization exemplar (or inherited exemplar)
        IDX_JS_NATIVE_MAX  // details len [source module handle]
    );

    heapaddr_t native_id = Native_Id_For_Action(native);

    REBARR *details = ACT_DETAILS(native);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), source);  // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At(source)  // might change
        );
    }

    // !!! A bit wasteful to use a whole cell for this--could just be whether
    // the ID is positive or negative.  Keep things clear, optimize later.
    //
    Init_Logic(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER), REF(awaiter));

    // In the pthread build, if we're on the worker we have to synchronously
    // run the registration on the main thread.  Continuing without blocking
    // would be bad (what if they run the function right after declaring it?)
    //
    // However, there is no `main_thread_emscripten_run_script()`.  :-(
    // But there is MAIN_THREAD_EM_ASM(), so call a service routine w/that.
    // (The EM_ASMs compile JS inline directly with limited parameterization.
    // If we tried to use it with the text of a whole function body to add
    // to JS here, we'd have to escape it for `eval()-ing`...it gets ugly.)
    //
    MAIN_THREAD_EM_ASM("_RL_rebRegisterNative_internal($0)", native_id);

    // !!! Natives on the stack can specify where APIs like reb.Run() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    Init_Handle_Cdata_Managed(
        ARR_AT(details, IDX_JS_NATIVE_HANDLE),
        ACT_PARAMLIST(native),
        0,
        &cleanup_js_native
    );

    TERM_ARRAY_LEN(details, IDX_JS_NATIVE_MAX);
    SET_ACTION_FLAG(native, IS_NATIVE);

    return Init_Action_Unbound(D_OUT, native);
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
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_INIT_JAVASCRIPT_EXTENSION;

    TRACE("INIT-JAVASCRIPT-EXTENSION called");

  #if defined(USE_PTHREADS)
    int ret = 0;

    PG_Main_Thread = pthread_self();  // remember for debug checks

    ret |= pthread_create(
        &PG_Worker_Thread,
        nullptr,  // pthread attributes (optional)
        &promise_worker,
        m_cast(REBVAL*, END_NODE)  // unused arg (reads global state directly)
    );
    ret |= pthread_mutex_init(&PG_Promise_Mutex, nullptr);
    ret |= pthread_cond_init(&PG_Promise_Cond, nullptr);
    ret |= pthread_mutex_init(&PG_Await_Mutex, nullptr);
    ret |= pthread_cond_init(&PG_Await_Cond, nullptr);

    if (ret != 0)
        fail ("non-zero pthread API result in INIT-JAVASCRIPT-EXTENSION");
  #endif

    ENDIFY_POINTER_IF_DEBUG(PG_Native_Result);
    PG_Native_State = NATIVE_STATE_NONE;

    return Init_Void(D_OUT);
}


//
//  export js-trace: native [
//
//  {Internal debug tool for seeing what's going on in JavaScript dispatch}
//
//      return: <void>
//      enable [logic!]
//  ]
//
REBNATIVE(js_trace)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_TRACE;

  #ifdef DEBUG_JAVASCRIPT_EXTENSION
    PG_Probe_Failures = PG_JS_Trace = VAL_LOGIC(ARG(enable));
  #else
    fail ("JS-TRACE only if DEBUG_JAVASCRIPT_EXTENSION set in %emscripten.r");
  #endif

    return Init_Void(D_OUT);
}


// !!! Need shutdown, but there's currently no module shutdown
//
// https://forum.rebol.info/t/960
