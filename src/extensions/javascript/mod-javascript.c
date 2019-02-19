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

#include "sys-core.h"

#include "tmp-mod-javascript.h"

#include <limits.h>  // for UINT_MAX

#include <emscripten.h>

#if defined(USE_EMTERPRETER) == defined(USE_PTHREADS)
    #error "Define one (and only one) of USE_EMTERPRETER or USE_PTHREADS"
#endif

#if defined(USE_PTHREADS)
    #include <pthread.h>  // C11 threads not much better (and less portable)
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

#ifdef DEBUG_JAVASCRIPT_EXTENSION
    #undef assert  // if it was defined (most emscripten builds are NDEBUG)
    #define assert(expr) \
        if (!(expr)) { \
            printf("%s:%d - assert(%s)\n", __FILE__, __LINE__, #expr); \
            exit(0); \
        }

    bool PG_JS_Trace = false;  // Can be turned on/off with JS-TRACE native

    #define TRACE(...)  /* variadic, but emscripten is at least C99! :-) */ \
        do { if (PG_JS_Trace) { \
            printf("@%ld: ", cast(long, TG_Tick));  /* tick count prefix */ \
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
        cast(void*, p) == cast(void*, m_cast(REBVAL*, END_NODE))

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

typedef unsigned int heapaddr_t;

inline static heapaddr_t Heapaddr_From_Pointer(void *p) {
    intptr_t i = cast(intptr_t, cast(void*, p));
    assert(i < UINT_MAX);
    return i;
}

static void* Pointer_From_Heapaddr(heapaddr_t addr)
  { return cast(void*, cast(intptr_t, addr)); }


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

#define IDX_JS_NATIVE_HANDLE \
    IDX_NATIVE_MAX  // handle gives hookpoint for GC of table entry

#define IDX_JS_NATIVE_IS_AWAITER \
    (IDX_NATIVE_MAX + 1)  // LOGIC! of if this is an awaiter or not

#define IDX_JS_NATIVE_MAX \
    (IDX_JS_NATIVE_IS_AWAITER + 1)

REB_R JavaScript_Dispatcher(REBFRM *f);

static void cleanup_js_native(const REBVAL *v) {
    REBARR *paramlist = ARR(VAL_HANDLE_POINTER(REBARR*, v));
    heapaddr_t native_id = Heapaddr_From_Pointer(paramlist);
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

struct Reb_Promise_Info *PG_Promises;  // Singly-linked list

#if defined(USE_PTHREADS)

    // For why pthread conditions need a mutex:
    // https://stackoverflow.com/q/2763714/

    static pthread_t PG_Main_Thread;
    static pthread_mutex_t PG_Main_Mutex;
    static pthread_cond_t PG_Main_Cond;

    static pthread_t PG_Promise_Thread;
    static pthread_mutex_t PG_Promise_Mutex;
    static pthread_cond_t PG_Promise_Cond;

    // Information cannot be exchanged between the worker thread and the main
    // thread via JavaScript values, so they are proxied between threads as
    // heap pointers via these globals.
    //
    static REBVAL *PG_Promise_Result;
    static REBVAL *PG_Native_Result;

    inline static void ASSERT_ON_MAIN_THREAD() {  // in a browser, this is GUI
        if (not pthread_equal(pthread_self(), PG_Main_Thread))
            assert(!"Expected to be on MAIN thread but wasn't");
    }

    inline static void ASSERT_ON_PROMISE_THREAD() {
        if (not pthread_equal(pthread_self(), PG_Promise_Thread))
            assert(!"Didn't expect to be on MAIN thread but was\n");
    }
#else
    #define ASSERT_ON_PROMISE_THREAD()      NOOP
    #define ASSERT_ON_MAIN_THREAD()         NOOP
#endif


// rebPromise() the API augments the output of this RL_rebPromise() primitive.
//
// This returns an integer of a unique memory address it allocated to use in
// a mapping for the [resolve, reject] functions.  We will trigger those
// mappings when the promise is fulfilled.  In order to come back and do that
// fulfillment, it either puts the code processing into a timer callback
// (emterpreter) or queues it to a thread (pthreads).
//
// The resolve will be called if it reaches the end of the input and the
// reject if there is a failure.
//
// See %make-reb-lib.r for code that produces the `rebPromise(...)` API, which
// ties the returned integer into the resolve and reject branches of an
// actual JavaScript Promise.
//
EMSCRIPTEN_KEEPALIVE EXTERN_C
intptr_t RL_rebPromise(void *p, va_list *vaptr)
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

    DECLARE_VA_FEED (
        feed,
        p,
        vaptr,
        FEED_MASK_DEFAULT  // !!! Should top frame flags be heeded?
            | (FS_TOP->feed->flags.bits & FEED_FLAG_CONST)
    );

    // !!! This code inlined from Do_Va_Throws(), go over it to see if it's
    // really all necessary.
    //
    REBDSP dsp_orig = DSP;
    while (NOT_END(feed->value)) {
        Derelativize(DS_PUSH(), feed->value, feed->specifier);
        SET_CELL_FLAG(DS_TOP, UNEVALUATED);

        // SEE ALSO: The `inert:` branch in %c-eval.c, which is similar.  We
        // want `loop 2 [append '(a b c) 'd]` to be an error, which means the
        // quoting has to get the const flag from the frame if intended.
        //
        if (not GET_CELL_FLAG(feed->value, EXPLICITLY_MUTABLE))
            DS_TOP->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);

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

    EM_ASM(
        { setTimeout(function() { _RL_rebIdle_internal(); }, 0); }
    );  // note `_RL` (leading underscore means no cwrap)

    return info->promise_id;
}


#if defined(USE_PTHREADS)

// Worker pthread that loops, picks up promise work items, and runs the
// associated array of code.
//
void *promise_worker(void *vargp)
{
    assert(IS_POINTER_END_DEBUG(vargp));  // unused arg (reads PG_Promises)

    ASSERT_ON_PROMISE_THREAD();

    while (true) {
        //
        // Wait until we're signaled that MAIN won't make any libRebol calls.
        // (the signal comes from rebIdle(), which blocks the MAIN)
        //
        TRACE("promise_worker() => waiting on promise condition");
        pthread_cond_wait(&PG_Promise_Cond, &PG_Promise_Mutex);
        pthread_mutex_unlock(&PG_Promise_Mutex);
        TRACE("promise_worker() => got signal to start running promise");

        // There should be a promise ready to run if we're awoken here.

        struct Reb_Promise_Info *info = PG_Promises;
        REBARR *code = ARR(Pointer_From_Heapaddr(info->promise_id));
        assert(IS_POINTER_END_DEBUG(PG_Promise_Result));
        assert(info->state == PROMISE_STATE_QUEUEING);
        info->state = PROMISE_STATE_RUNNING;

        // !!! In the pthread case, we are running this promise not on the MAIN
        // thread.  That means this is top level.  We have to push a trap.
        // But as this is JavaScript, we only need to handle the JS-specific
        // fails...which rebTrap() is supposed to abstract.  Review.
        //
        // !!! reject() should probably be called on error.  JavaScript's
        // analogue to FAIL is throw(), and the difference between `throw()`
        // and `reject()` in JS is subtle.
        //
        // https://stackoverflow.com/q/33445415/

        REBVAL *result = Alloc_Value();
        if (Do_At_Mutable_Throws(result, code, 0, SPECIFIED)) {
            //
            // !!! This should presumably be a reject(), see note above.
            // !!! Nothing to catch this fail() on the non-MAIN thread
            //
            fail (Error_No_Catch_For_Throw(result));  // result auto-releases
        }

        info->state = PROMISE_STATE_RESOLVED;
        PG_Promise_Result = result;

        // Signal MAIN to unblock.  (Any time rebIdle() is unblocked, it needs
        // to check promise_result to see if it's ready, or if there's a
        // request to run some other code)
        //
        TRACE("promise_worker() => signaling MAIN to unblock");
        pthread_mutex_lock(&PG_Main_Mutex);
        pthread_cond_signal(&PG_Main_Cond);
        pthread_mutex_unlock(&PG_Main_Mutex);
        TRACE("promise_worker() => MAIN unblocked, finishing up");
    }
}

#endif  // defined(USE_PTHREADS)


REBVAL *Get_Native_Result(intptr_t frame_id)
{
    ASSERT_ON_MAIN_THREAD();
    heapaddr_t result_addr = EM_ASM_INT(
        { return reb.GetNativeResult_internal($0) },
        frame_id  // => $0
    );
    return VAL(Pointer_From_Heapaddr(result_addr));
}

#ifdef USE_PTHREADS
    void Proxy_Native_Result_To_Worker(intptr_t frame_id) {
        //
        // We can get the result now...and need to.  (We won't be able to
        // access MAIN variables on the MAIN thread.)
        //
        assert(IS_POINTER_END_DEBUG(PG_Native_Result));
        PG_Native_Result = Get_Native_Result(frame_id);
        EM_ASM(
            { setTimeout(function() { _RL_rebIdle_internal(); }, 0); }
        );  // note `_RL` (leading underscore means no cwrap)
    }
#endif

void Invoke_JavaScript_Native(REBFRM *f)
{
    ASSERT_ON_MAIN_THREAD();
    struct Reb_Promise_Info *info = PG_Promises;

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    bool is_awaiter = VAL_LOGIC(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER));

    heapaddr_t native_id = Heapaddr_From_Pointer(ACT_PARAMLIST(FRM_PHASE(f)));
    heapaddr_t frame_id = Heapaddr_From_Pointer(f);  // unique pointer

    TRACE("Invoke_JavaScript_Native(%s)", Frame_Label_Or_Anonymous_UTF8(f));

    if (is_awaiter) {
        //
        // We pre-emptively set the state to awaiting, in case it gets
        // resolved during the JavaScript of the awaiter's body--so we can
        // detect a transition back to RUNNING (e.g. resolved).
        //
        assert(info and info->state == PROMISE_STATE_RUNNING);
        info->state = PROMISE_STATE_AWAITING;

        EM_ASM(
            { reb.RunNativeAwaiter_internal($0, $1) },
            native_id,  // => $0
            frame_id  // => $1
        );
    }
    else {
        assert(not info or info->state == PROMISE_STATE_RUNNING);

        EM_ASM(
            { reb.RunNative_internal($0, $1) },
            native_id,  // => $0
            frame_id  // = $1
        );
    }
}


// This is the code that rebPromise() defers to run until there is no
// JavaScript above it or after it on the MAIN thread stack.  This makes it
// safe to use emscripten_sleep_with_yield() inside of it in the emterpreter
// build, and it purposefully holds up the MAIN from running in the pthread
// version so that both threads don't call libRebol APIs at once.
//
EMSCRIPTEN_KEEPALIVE EXTERN_C
void RL_rebIdle_internal(void)  // there should be NO user JS code on stack!
{
    ASSERT_ON_MAIN_THREAD();

    struct Reb_Promise_Info *info = PG_Promises;

    // Each call to rebPromise() queues an idle cycle, but we never process
    // more than one promise per idle.  (We may not finish a promise depending
    // on how many JS-NATIVE and JS-AWAITER calls there are.)  There should
    // always be at least one promise to be dealt with--maybe an unfinished
    // one from before.
    //
    assert(info != nullptr);

    REBARR *code = ARR(Pointer_From_Heapaddr(info->promise_id));
    if (info->state == PROMISE_STATE_QUEUEING) {
        assert(NOT_SERIES_FLAG(code, MANAGED));  // took off so it didn't GC
        SET_SERIES_FLAG(code, MANAGED);  // but need it back on to execute it
    }

    REBVAL *result;

  #if defined(USE_EMTERPRETER)

    assert(code);
    assert(info->state == PROMISE_STATE_QUEUEING);
    info->state = PROMISE_STATE_RUNNING;

    // REVIEW: PUSH_TRAP() / reject()?

    // The "simple" case: we actually stay on the MAIN thread, and JS-AWAITER
    // can call emscripten_sleep_with_yield()...which will just post any
    // requests to continue as a setTimeout().  Emscripten does all the work.
    //
    TRACE("rebIdle() => begin emterpreting promise code");
    result = Alloc_Value();
    if (Do_At_Mutable_Throws(result, code, 0, SPECIFIED)) {
        //
        // !!! This should presumably be a reject(), see note above.
        // !!! Nothing to catch this fail() on the non-MAIN thread
        //
        fail (Error_No_Catch_For_Throw(result));  // will release result
    }
    TRACE("rebIdle() => end emterpreting promise code");

    assert(info->state == PROMISE_STATE_RUNNING);
    info->state = PROMISE_STATE_RESOLVED;

  #else  // PTHREAD model

    // Harder (but worth it to be able to run WASM direct and not through a
    // bloated and slow bytecode!)  We spawn a thread and have to forcibly
    // block the MAIN so it doesn't keep running (because it could potentially
    // execute more libRebol code and crash the running promise--Rebol isn't
    // multithreaded.)

    // We're about to signal the promise, which may turn around and signal
    // us right back...be sure we guard the signaling so we don't miss it.
    //
    pthread_mutex_lock(&PG_Main_Mutex);

    // We guarantee that the MAIN is at the top level and not running API code,
    // so it's time to signal the promise to make some progress.
    //
    if (info->state == PROMISE_STATE_QUEUEING)
        TRACE("rebIdle() => waking up worker to start promise");
    else if (info->state == PROMISE_STATE_AWAITING)
        TRACE("rebIdle() => waking up worker to resume after awaiting");
    else if (info->state == PROMISE_STATE_RUNNING)
        TRACE("rebIdle() => waking up worker after non-awaiter native");
    else
        assert(!"rebIdle() bad promise state");
    pthread_mutex_lock(&PG_Promise_Mutex);
    pthread_cond_signal(&PG_Promise_Cond);
    pthread_mutex_unlock(&PG_Promise_Mutex);

    // Have to put the MAIN on hold so it doesn't make any libRebol API calls
    // while the promise is running its own code that might make some.
    //
    TRACE("rebIdle() => blocking MAIN until wakeup signal");
    pthread_cond_wait(&PG_Main_Cond, &PG_Main_Mutex);
    pthread_mutex_unlock(&PG_Main_Mutex);
    TRACE("rebIdle() => MAIN unblocked");

    // The MAIN can be unblocked for several reasons--but in all of these
    // the promise thread should not be running right now.  Either it is
    // finished or it is suspended waiting on a condition to continue.

    if (info->state == PROMISE_STATE_RESOLVED) {
        result = PG_Promise_Result;
        ENDIFY_POINTER_IF_DEBUG(PG_Promise_Result);
    }
    else {
        // We didn't finish, and the reason we're unblocking is because a
        // JS-NATIVE wants to run.  It wants us to run the body text of the
        // JavaScript here on the MAIN, because that's where it is useful.
        //
        REBACT *phase = FRM_PHASE(FS_TOP);
        assert(ACT_DISPATCHER(phase) == &JavaScript_Dispatcher);

        REBARR *details = ACT_DETAILS(phase);
        bool is_awaiter = VAL_LOGIC(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER));

        TRACE("rebIdle() => Invoke_JavaScript_Native()");
        Invoke_JavaScript_Native(FS_TOP);

        if (not is_awaiter) {
            //
            // During an await in the pthread model, libRebol routines can be
            // called from the MAIN thread (just not promises and awaiters).
            // So that includes plain JavaScript natives.  But that native
            // is blocked here.  Solve it by requeuing idle for now.
            //
            heapaddr_t frame_id = Heapaddr_From_Pointer(FS_TOP);
            Proxy_Native_Result_To_Worker(frame_id);
        }

        // The awaiter *might* have called resolve() or reject() in its body.
        // we could handle that and loop here, but the average case is that
        // it called JavaScript to do some MAIN work.  Since any resolve() or
        // reject() requeues rebIdle() anyway, have that case handled by the
        // next run of rebIdle() vs. a goto above here.

        result = m_cast(REBVAL*, END_NODE);  // avoid compiler warning
    }
  #endif

    if (info->state == PROMISE_STATE_RESOLVED) {
        if (IS_NULLED(result)) {
            rebRelease(result);
            result = nullptr;
        }

        EM_ASM(
            { reb.ResolvePromise_internal($0, $1); },
            info->promise_id,  // => $0 (promise table entry will be freed)
            result  // => $1 (recipient takes over handle)
        );

        assert(PG_Promises == info);
        PG_Promises = info->next;
        FREE(struct Reb_Promise_Info, info);
    }

    TRACE("rebIdle() => exiting to generic MAIN processing");
}


// This is rebSignalAwaiter() and not rebResolveAwaiter() which passes in a
// resolution value because the emterpreter build can't really pass a REBVAL*.
// All the APIs it would need to make REBVAL* are unavailable.  So it instead
// pokes a JavaScript function where it can be found when no longer in
// emscripten_sleep_with_yield(), then reb.GetAwaiterResult_internal() is used
// to ask for that value later.
//
// The pthreads build *could* take a value and poke it into the promise info.
// But it's not worth it to wire up two different protocols on the JavaScript
// side.  It should be rethought if someday the emterpreter version is axed.
//
EMSCRIPTEN_KEEPALIVE EXTERN_C
void RL_rebSignalAwaiter_internal(intptr_t frame_id, int rejected) {
    ASSERT_ON_MAIN_THREAD();

    struct Reb_Promise_Info *info = PG_Promises;
    assert(info->state == PROMISE_STATE_AWAITING);

    if (rejected == 0) {
        TRACE("reb.SignalAwaiter_internal() => signaling resolve");

        // If we resolved the awaiter, we didn't resolve the overall promise,
        // but just pushed it back into the running state.
        //
        info->state = PROMISE_STATE_RUNNING;
    }
    else {
        assert(rejected == 1);
        TRACE("reb.SignalAwaiter_internal() => signaling reject");

        // !!! Rejecting an awaiter basically means the promise as a whole
        // failed, I'd assume?
        //
        info->state = PROMISE_STATE_REJECTED;
    }

  #if defined(USE_PTHREADS)
    Proxy_Native_Result_To_Worker(frame_id);
  #endif

    // The above only signaled.  The awaiter will get the actual result with
    // reb.GetAwaiterResult_internal().
}


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
    heapaddr_t frame_id = Heapaddr_From_Pointer(f);  // unique pointer

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

    REBVAL *result;

  #if defined(USE_EMTERPRETER)
    //
    // We're on the MAIN thread, by definition (there only is a MAIN thread)
    // Pre-emptively set the state to awaiting, because resolve() or reject()
    // may be called during the body.

    Invoke_JavaScript_Native(f);

    // We don't know exactly what MAIN event is going to trigger and cause a
    // resolve() to happen.  It could be a timer, it could be a fetch(),
    // it could be anything.  The emterpreted build doesn't really have a
    // choice other than to poll...there's nothing like pthread wait
    // conditions available.  We wait at least 50msec (probably more, as
    // we don't control how long the MAIN will be running whatever it does).
    //
    TRACE("JavaScript_Dispatcher() => begin sleep_with_yield() loop");
    while (info and info->state == PROMISE_STATE_AWAITING) {  // !!! volatile?
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

    result = Get_Native_Result(frame_id);
  #else
    // If we're already on the MAIN thread, then we're just calling a JS
    // service routine with no need to yield.
    //
    if (pthread_equal(pthread_self(), PG_Main_Thread)) {
        assert(not is_awaiter);
        Invoke_JavaScript_Native(f);
        result = Get_Native_Result(frame_id);
    }
    else {
        // We are not using the emterpreter, so we have to block our return
        // on a condition, while signaling the MAIN that it can go ahead and
        // run.  The MAIN has to actually run the JS code.

        assert(IS_POINTER_END_DEBUG(PG_Native_Result));

        // We are going to be signaled when a resolve() or reject() happens,
        // but might miss the signal if there weren't coordination that the
        // signal would only be given when we were waiting.
        //
        pthread_mutex_lock(&PG_Promise_Mutex);

        TRACE("JavaScript_Dispatcher() => signaling MAIN wakeup");
        pthread_mutex_lock(&PG_Main_Mutex);
        pthread_cond_signal(&PG_Main_Cond);
        pthread_mutex_unlock(&PG_Main_Mutex);

        // Block until the MAIN says that what we're awaiting on is resolved
        // (or rejected...TBD)
        //
        TRACE("JavaScript_Dispatcher() => suspending for native result");
        pthread_cond_wait(&PG_Promise_Cond, &PG_Promise_Mutex);
        pthread_mutex_unlock(&PG_Promise_Mutex);
        TRACE("JavaScript_Dispatcher() => native result was signaled");

        result = PG_Native_Result;
        ENDIFY_POINTER_IF_DEBUG(PG_Native_Result);

        // MAIN should be locked again at this point
    }
  #endif

    assert(not info or info->state == PROMISE_STATE_RUNNING);  // reject?
    return result;
}


//
//  export js-native: native [
//
//  {Create ACTION! from textual JavaScript code}
//
//      return: [action!]
//      spec "Function specification (similar to the one used by FUNCTION)"
//          [block!]
//      source "JavaScript code as a text string" [text!]
//      /awaiter "implicit resolve()/reject() parameters signal return result"
//  ]
//
REBNATIVE(js_native)  // specialized as JS-AWAITER in %ext-javascript-init.reb
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

    heapaddr_t native_id = Heapaddr_From_Pointer(paramlist);

    REBARR *details = ACT_DETAILS(native);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_BODY), source);  // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_BODY),
            Copy_String_At_Len(source, -1)  // might change
        );
    }

    // !!! A bit wasteful to use a whole cell for this--could just be whether
    // the ID is positive or negative.  Keep things clear, optimize later.
    //
    Init_Logic(ARR_AT(details, IDX_JS_NATIVE_IS_AWAITER), REF(awaiter));

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Unencoded(mo->series, "reb.RegisterId_internal(");

    REBYTE buf[60];  // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(buf, native_id);
    Append_Unencoded_Len(mo->series, s_cast(buf), len);

    Append_Unencoded(mo->series, ", function() {\n");  // would add ID number
    Append_Unencoded(mo->series, "return function");  // !!!! async function?
    Append_Unencoded(mo->series, REF(awaiter) ? "(resolve, reject)" : "()");
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

    Append_Unencoded(mo->series, "}\n");  // end `function() {`
    Append_Unencoded(mo->series, "}()");  // invoke dummy function
    Append_Unencoded(mo->series, ");");  // end `reb.RegisterId_internal(`

    TERM_SERIES(mo->series);

    TRACE("Registering native_id %d", native_id);
    emscripten_run_script(cs_cast(BIN_AT(mo->series, mo->start)));

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like reb.Run() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    Init_Handle_Managed(
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
    ret |= pthread_mutex_init(&PG_Main_Mutex, nullptr);
    ret |= pthread_cond_init(&PG_Main_Cond, nullptr);

    ret |= pthread_create(
        &PG_Promise_Thread,
        nullptr,  // pthread attributes (optional)
        &promise_worker,
        m_cast(REBVAL*, END_NODE)  // unused arg (reads global state directly)
    );
    ret |= pthread_mutex_init(&PG_Promise_Mutex, nullptr);
    ret |= pthread_cond_init(&PG_Promise_Cond, nullptr);

    if (ret != 0)
        fail ("non-zero pthread API result in INIT-JAVASCRIPT-EXTENSION");

    ENDIFY_POINTER_IF_DEBUG(PG_Promise_Result);
    ENDIFY_POINTER_IF_DEBUG(PG_Native_Result);
  #endif

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
