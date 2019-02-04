//
//  File: %sys-eval.h
//  Summary: {Low-Level Internal Evaluator API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// The primary routine that performs DO and EVALUATE is Eval_Core_Throws().
// It takes one parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack...and fail() is
// written such that a longjmp up to a failure handler above it can run
// safely and clean up even though intermediate stacks have vanished.
//
// Ren-C can run the evaluator across a REBARR-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as REBVAL* to comma-separated input at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! REBVAL in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when REBVAL pointers that
// are originating with C source were intended to be supplied to a function
// with no evaluation.  In R3-Alpha, the only way in an evaluative context
// to suppress such evaluations would be by adding elements (such as QUOTE).
// Besides the cost and labor of inserting these, the risk is that the
// intended functions to be called without evaluation, if they quoted
// arguments would then receive the QUOTE instead of the arguments.
//
// The problem was solved by adding a feature to the evaluator which was
// also opened up as a new privileged native called EVAL.  EVAL's refinements
// completely encompass evaluation possibilities in R3-Alpha, but it was also
// necessary to consider cases where a value was intended to be provided
// *without* evaluation.  This introduced EVAL/ONLY.
//


// This is a very light wrapper over Eval_Core_Throws(), which is used with
// operations like ANY or REDUCE that wish to perform several successive
// operations on an array, without creating a new frame each time.
//
inline static bool Eval_Step_Throws(REBVAL *out, REBFRM *f) {
    assert(IS_END(out));

    assert(NOT_EVAL_FLAG(f, TO_END));
    assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
    assert(NOT_FEED_FLAG(f->feed, BARRIER_HIT));

    f->out = out;
    f->dsp_orig = DSP;
    bool threw = (*PG_Eval_Throws)(f);  // should already be pushed

    return threw;
}


// Unlike Eval_Step_Throws() which relies on tests of IS_END() on out to
// see if the end was reached, this expects the caller to preload the output
// with some value, and then test OUT_MARKED_STALE to see if the only thing
// run in the frame were invisibles (empty groups, comments) or nothing.
//
inline static bool Eval_Step_Maybe_Stale_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(NOT_END(out));

    assert(NOT_EVAL_FLAG(f, TO_END));
    assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
    assert(NOT_FEED_FLAG(f->feed, BARRIER_HIT));

    f->out = out;
    f->dsp_orig = DSP;
    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

    return threw;
}


// Bit heavier wrapper of Eval_Core_Throws() than Eval_Step_In_Frame_Throws().
// It also reuses the frame...but has to clear and restore the frame's
// flags.  It is currently used only by SET-WORD! and SET-PATH!.
//
// Note: Consider pathological case `x: eval lit y: eval eval lit z: ...`
// This can be done without making a new frame, but the eval cell which holds
// the SET-WORD! needs to be put back in place before returning, so that the
// set knows where to write.  The caller handles this with the data stack.
//
inline static bool Eval_Step_Mid_Frame_Throws(REBFRM *f, REBFLGS flags) {
    assert(f->dsp_orig == DSP);

    REBFLGS prior_flags = f->flags.bits;
    f->flags = Endlike_Header(flags);

    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

    f->flags.bits = prior_flags; // e.g. restore EVAL_FLAG_TO_END    
    return threw;
}


// It should not be necessary to use a subframe unless there is meaningful
// state which would be overwritten in the parent frame.  For the moment,
// that only happens if a function call is in effect -or- if a SET-WORD! or
// SET-PATH! are running with an expiring `current` in effect.  Else it is
// more efficient to call Eval_Step_In_Frame_Throws(), or the also lighter
// Eval_Step_In_Mid_Frame_Throws().
//
// !!! This operation used to try and optimize some cases without using a
// subframe.  But checking for whether an optimization would be legal or not
// was complex, as even something inert like `1` cannot be evaluated into a
// slot as `1` unless you are sure there's no `+` or other enfixed operation.
// Over time as the evaluator got more complicated, the redundant work and
// conditional code paths showed a slight *slowdown* over just having an
// inline function that built a frame and recursed Eval_Core_Throws().
//
// Future investigation could attack the problem again and see if there is
// any common case that actually offered an advantage to optimize for here.
//
inline static bool Eval_Step_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *f,
    REBFLGS flags
){
    assert(not IS_END(f->feed->value));  // would be wasting time to call

    DECLARE_FRAME(subframe, f->feed, flags);

    Push_Frame(out, subframe);
    bool threw = (*PG_Eval_Throws)(subframe);
    Drop_Frame(subframe);

    return threw;
}


inline static bool Reevaluate_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *f,
    const REBVAL *reval,
    REBFLGS flags
){
    DECLARE_FRAME(subframe, f->feed, flags | EVAL_FLAG_REEVALUATE_CELL);
    subframe->u.reval.value = reval;

    Push_Frame(out, subframe);
    bool threw = (*PG_Eval_Throws)(subframe);
    Drop_Frame(subframe);

    return threw;
}

// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.
//
inline static REBIXO Eval_Array_At_Core(
    REBVAL *out, // must be initialized, marked stale if empty / all invisible
    const RELVAL *opt_first, // non-array element to kick off execution with
    REBARR *array,
    REBCNT index,
    REBSPC *specifier, // must match array, but also opt_first if relative
    REBFLGS flags // EVAL_FLAG_TO_END, EVAL_FLAG_EXPLICIT_EVALUATE, etc.
){
    struct Reb_Feed feed_struct;  // opt_first so can't use DECLARE_ARRAY_FEED
    struct Reb_Feed *feed = &feed_struct;
    Prep_Array_Feed(
        feed,
        opt_first,
        array,
        index,
        specifier,
        FEED_MASK_DEFAULT
    );

    if (IS_END(feed->value))
        return END_FLAG;

    DECLARE_FRAME (f, feed, flags);

    Push_Frame(out, f);
    bool threw = (*PG_Eval_Throws)(f);
    Drop_Frame(f);

    if (threw)
        return THROWN_FLAG;

    assert(
        not (flags & EVAL_FLAG_TO_END)
        or f->feed->index == ARR_LEN(array) + 1
    );
    return f->feed->index;
}


//
//  Reify_Va_To_Array_In_Frame: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of REBVAL*
// does not need to make a series to hold them.  Eval_Core is written to use
// the va_list traversal as an alternate to DO-ing an ARRAY.
//
// However, va_lists cannot be backtracked once advanced.  So in a debug mode
// it can be helpful to turn all the va_lists into arrays before running
// them, so stack frames can be inspected more meaningfully--both for upcoming
// evaluations and those already past.
//
// A non-debug reason to reify a va_list into an array is if the garbage
// collector needs to see the upcoming values to protect them from GC.  In
// this case it only needs to protect those values that have not yet been
// consumed.
//
// Because items may well have already been consumed from the va_list() that
// can't be gotten back, we put in a marker to help hint at the truncation
// (unless told that it's not truncated, e.g. a debug mode that calls it
// before any items are consumed).
//
inline static void Reify_Va_To_Array_In_Frame(
    REBFRM *f,
    bool truncated
) {
    REBDSP dsp_orig = DSP;

    assert(FRM_IS_VALIST(f));

    if (truncated) {
        DS_PUSH();
        Init_Word(DS_TOP, Canon(SYM___OPTIMIZED_OUT__));
    }

    if (NOT_END(f->feed->value)) {
        assert(f->feed->pending == END_NODE);

        do {
            Derelativize(DS_PUSH(), f->feed->value, f->feed->specifier);
            assert(not IS_NULLED(DS_TOP));
            Fetch_Next_Forget_Lookback(f);
        } while (NOT_END(f->feed->value));

        if (truncated)
            f->feed->index = 2; // skip the --optimized-out--
        else
            f->feed->index = 1; // position at start of the extracted values
    }
    else {
        assert(IS_POINTER_TRASH_DEBUG(f->feed->pending));

        // Leave at end of frame, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        f->feed->index = 0;
    }

    assert(not f->feed->vaptr); // feeding forward should have called va_end

    f->feed->array = Pop_Stack_Values(dsp_orig);
    MANAGE_ARRAY(f->feed->array); // held alive while frame running

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we're holding it.  (This would be more complex
    // if we reused the empty array if dsp_orig == DSP, since someone else
    // might have a hold on it...not worth the complexity.) 
    //
    assert(NOT_FEED_FLAG(f->feed, TOOK_HOLD));
    SET_SERIES_INFO(f->feed->array, HOLD);
    SET_FEED_FLAG(f->feed, TOOK_HOLD);

    if (truncated)
        f->feed->value = ARR_AT(f->feed->array, 1); // skip `--optimized--`
    else
        f->feed->value = ARR_HEAD(f->feed->array);

    f->feed->pending = f->feed->value + 1;
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// The evaluator has a common means of fetching values out of both arrays
// and C va_lists via Fetch_Next_In_Frame(), so this code can behave the
// same as if the passed in values came from an array.  However, when values
// originate from C they often have been effectively evaluated already, so
// it's desired that WORD!s or PATH!s not execute as they typically would
// in a block.  So this is often used with EVAL_FLAG_EXPLICIT_EVALUATE.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG, or VA_LIST_FLAG
//
inline static REBIXO Eval_Va_Core(
    REBVAL *out, // must be initialized, marked stale if empty / all invisible
    const void *opt_first,
    va_list *vaptr,
    REBFLGS flags
){
    DECLARE_VA_FEED (feed, opt_first, vaptr, FEED_MASK_DEFAULT);
    DECLARE_FRAME (f, feed, flags);

    if (IS_END(f->feed->value))
        return END_FLAG;

    Push_Frame(out, f);
    bool threw = (*PG_Eval_Throws)(f);
    Drop_Frame(f); // will va_end() if not reified during evaluation

    if (threw)
        return THROWN_FLAG;

    if (
        (flags & EVAL_FLAG_TO_END) // not just an EVALUATE, but a full DO
        or GET_CELL_FLAG(f->out, OUT_MARKED_STALE) // just ELIDEs and COMMENTs
    ){
        assert(IS_END(f->feed->value));
        return END_FLAG;
    }

    if ((flags & EVAL_FLAG_NO_RESIDUE) and NOT_END(f->feed->value))
        fail (Error_Apply_Too_Many_Raw());

    return VA_LIST_FLAG; // frame may be at end, next call might just END_FLAG
}


inline static bool Eval_Value_Core_Throws(
    REBVAL *out,
    const RELVAL *value, // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
){
    if (ANY_INERT(value)) {
        Derelativize(out, value, specifier);
        return false; // fast things that don't need frames (should inline)
    }

    REBIXO indexor = Eval_Array_At_Core(
        SET_END(out), // start with END to detect no actual eval product
        value, // put the value as the opt_first element
        EMPTY_ARRAY,
        0, // start index (it's an empty array, there's no added processing)
        specifier,
        (EVAL_MASK_DEFAULT & ~EVAL_FLAG_CONST)
            | EVAL_FLAG_TO_END
            | (FS_TOP->flags.bits & EVAL_FLAG_CONST)
            | (value->header.bits & EVAL_FLAG_CONST)
    );

    if (IS_END(out))
        fail ("Eval_Value_Core_Throws() empty or just COMMENTs/ELIDEs/BAR!s");

    return indexor == THROWN_FLAG;
}

#define Eval_Value_Throws(out,value) \
    Eval_Value_Core_Throws((out), (value), SPECIFIED)


// The evaluator accepts API handles back from action dispatchers, and the
// path evaluator accepts them from path dispatch.  This code does common
// checking used by both, which includes automatic release of the handle
// so the dispatcher can write things like `return rebRun(...);` and not
// encounter a leak.
//
inline static void Handle_Api_Dispatcher_Result(REBFRM *f, const REBVAL* r) {
    //
    // !!! There is no protocol in place yet for the external API to throw,
    // so that is something to think about.  At the moment, only f->out can
    // hold thrown returns, and these API handles are elsewhere.
    //
    assert(not Is_Evaluator_Throwing_Debug());

  #if !defined(NDEBUG)
    if (NOT_CELL_FLAG(r, ROOT)) {
        printf("dispatcher returned non-API value not in D_OUT\n");
        printf("during ACTION!: %s\n", f->label_utf8);
        printf("`return D_OUT;` or use `RETURN (non_api_cell);`\n");
        panic(r);
    }
  #endif

    if (IS_NULLED(r))
        assert(!"Dispatcher returned nulled cell, not C nullptr for API use");

    Move_Value(f->out, r);
    if (NOT_CELL_FLAG(r, MANAGED))
        rebRelease(r);
}
