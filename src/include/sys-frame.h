//
//  File: %sys-frame.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// A single FRAME! can go through multiple phases of evaluation, some of which
// should expose more fields than others.  For instance, when you specialize
// a function that has 10 parameters so it has only 8, then the specialization
// frame should not expose the 2 that have been removed.  It's as if the
// KEYS OF the spec is shorter than the actual length which is used.
//
// Hence, each independent value that holds a frame must remember the function
// whose "view" it represents.  This field is only applicable to frames, and
// so it could be used for something else on other types
//
// Note that the binding on a FRAME! can't be used for this purpose, because
// it's already used to hold the binding of the function it represents.  e.g.
// if you have a definitional return value with a binding, and try to
// MAKE FRAME! on it, the paramlist alone is not enough to remember which
// specific frame that function should exit.
//

// !!! Find a better place for this!
//
inline static bool IS_QUOTABLY_SOFT(const RELVAL *v) {
    return IS_GROUP(v) or IS_GET_WORD(v) or IS_GET_PATH(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


inline static bool FRM_IS_VALIST(REBFRM *f) {
    return f->feed->vaptr != nullptr;
}

inline static REBARR *FRM_ARRAY(REBFRM *f) {
    assert(IS_END(f->feed->value) or not FRM_IS_VALIST(f));
    return f->feed->array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBCNT FRM_INDEX(REBFRM *f) {
    if (IS_END(f->feed->value))
        return ARR_LEN(f->feed->array);

    assert(not FRM_IS_VALIST(f));
    return f->feed->index - 1;
}

inline static REBCNT FRM_EXPR_INDEX(REBFRM *f) {
    assert(not FRM_IS_VALIST(f));
    return f->expr_index - 1;
}

inline static REBSTR* FRM_FILE(REBFRM *f) { // https://trello.com/c/K3vntyPx
    if (not f->feed->array)
        return nullptr;
    if (NOT_ARRAY_FLAG(f->feed->array, HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return STR(LINK(f->feed->array).custom.node);
}

inline static const char* FRM_FILE_UTF8(REBFRM *f) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(__ANONYMOUS__).
    //
    REBSTR *str = FRM_FILE(f);
    return str ? STR_UTF8(str) : "(anonymous)"; 
}

inline static int FRM_LINE(REBFRM *f) {
    if (not f->feed->array)
        return 0;
    if (NOT_ARRAY_FLAG(f->feed->array, HAS_FILE_LINE_UNMASKED))
        return 0;
    return MISC(SER(f->feed->array)).line;
}

#define FRM_OUT(f) \
    cast(REBVAL * const, (f)->out) // writable rvalue


// Note about FRM_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    (cast(REBSER*, (f)->varlist)->content.dynamic.used - 1) // minus rootvar

#define FRM_SPARE(f) \
    cast(REBVAL*, &(f)->spare)

#define FRM_PRIOR(f) \
    ((f)->prior + 0) // prevent assignment via this macro

#define FRM_PHASE(f) \
    VAL_PHASE_UNCHECKED((f)->rootvar)  // shoud be valid--unchecked for speed

#define INIT_FRM_PHASE(f,phase) \
    INIT_VAL_CONTEXT_PHASE((f)->rootvar, (phase))

#define FRM_BINDING(f) \
    EXTRA(Binding, (f)->rootvar).node

#define FRM_UNDERLYING(f) \
    ACT_UNDERLYING((f)->original)

#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // prevent assignment via this macro


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define FRM_ARGS_HEAD(f) \
    ((f)->rootvar + 1)

#ifdef NDEBUG
    #define FRM_ARG(f,n) \
        ((f)->rootvar + (n))
#else
    inline static REBVAL *FRM_ARG(REBFRM *f, REBCNT n) {
        assert(n != 0 and n <= FRM_NUM_ARGS(f));

        REBVAL *var = f->rootvar + n; // 1-indexed
        assert(not IS_RELATIVE(cast(RELVAL*, var)));
        return var;
    }
#endif


inline static void Get_Frame_Label_Or_Blank(RELVAL *out, REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        Init_Word(out, f->opt_label); // invoked via WORD! or PATH!
    else
        Init_Blank(out); // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        return STR_UTF8(f->opt_label);
    return "[anonymous]";
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Eval_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (f), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each EVALUATE step.
//
// Like higher level APIs that move through the input series, this low-level
// API can move at full EVALUATE intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after an EVALUATE on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// f->value with a valid "fetched" REBVAL*.
//
// This privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.

inline static void Reuse_Varlist_If_Available(REBFRM *f) {
    assert(IS_POINTER_TRASH_DEBUG(f->varlist));
    if (not TG_Reuse)
        f->varlist = nullptr;
    else {
        f->varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        f->rootvar = cast(REBVAL*, SER(f->varlist)->content.dynamic.data);
        LINK_KEYSOURCE(f->varlist) = NOD(f);
    }
}

inline static void Push_Frame_No_Varlist(REBVAL *out, REBFRM *f)
{
    assert(f->feed->value != nullptr);

    // Frames are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use null until first eval call if needed
    //
    f->out = out;

    // All calls through to Eval_Core() are assumed to happen at the same C
    // stack level for a pushed frame (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Eval_Step_Throws() for `reduce [a | b | ... | z]`.
    //
    if (C_STACK_OVERFLOWING(&f))
        Fail_Stack_Overflow();

    assert(SECOND_BYTE(f->flags) == 0); // END signal
    assert(not (f->flags.bits & NODE_FLAG_CELL));

    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
    // !!! A non-contiguous data stack which is not a series is a possibility.
    //
  #ifdef STRESS_CHECK_DO_OUT_POINTER
    REBNOD *containing;
    if (
        did (containing = Try_Find_Containing_Node_Debug(f->out))
        and not (containing->header.bits & NODE_FLAG_CELL)
        and NOT_SERIES_FLAG(containing, DONT_RELOCATE)
    ){
        printf("Request for ->out location in movable series memory\n");
        panic (containing);
    }
  #else
    assert(not IN_DATA_STACK_DEBUG(f->out));
  #endif

  #ifdef DEBUG_EXPIRED_LOOKBACK
    f->stress = nullptr;
  #endif

    // The arguments to functions in their frame are exposed via FRAME!s
    // and through WORD!s.  This means that if you try to do an evaluation
    // directly into one of those argument slots, and run arbitrary code
    // which also *reads* those argument slots...there could be trouble with
    // reading and writing overlapping locations.  So unless a function is
    // in the argument fulfillment stage (before the variables or frame are
    // accessible by user code), it's not legal to write directly into an
    // argument slot.  :-/
    //
  #if !defined(NDEBUG)
    REBFRM *ftemp = FS_TOP;
    for (; ftemp != FS_BOTTOM; ftemp = ftemp->prior) {
        if (not Is_Action_Frame(ftemp))
            continue;
        if (Is_Action_Frame_Fulfilling_Unchecked(ftemp))
            continue;
        if (GET_SERIES_INFO(ftemp->varlist, INACCESSIBLE))
            continue; // Encloser_Dispatcher() reuses args from up stack
        assert(
            f->out < FRM_ARGS_HEAD(ftemp)
            or f->out >= FRM_ARGS_HEAD(ftemp) + FRM_NUM_ARGS(ftemp)
        );
    }
  #endif

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // REB_ACTION but doesn't have valid args/state.  The original action is a
    // good choice because it is only affected by the function call case,
    // see Is_Action_Frame_Fulfilling().
    //
    f->original = nullptr;

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif

  #if !defined(NDEBUG)
    //
    // !!! TBD: the relevant file/line update when f->feed->array changes
    //
    f->file = FRM_FILE_UTF8(f);
    f->line = FRM_LINE(f);
  #endif

    f->prior = TG_Top_Frame;
    TG_Top_Frame = f;

    // If the source for the frame is a REBARR*, then we want to temporarily
    // lock that array against mutations.  
    //
    if (FRM_IS_VALIST(f)) {
        //
        // There's nothing to put a hold on while it's a va_list-based frame.
        // But a GC might occur and "Reify" it, in which case the array
        // which is created will have a hold put on it to be released when
        // the frame is finished.
        //
        assert(NOT_FEED_FLAG(f->feed, TOOK_HOLD));
    }
    else {
        if (GET_SERIES_INFO(f->feed->array, HOLD))
            NOOP; // already temp-locked
        else {
            SET_SERIES_INFO(f->feed->array, HOLD);
            SET_FEED_FLAG(f->feed, TOOK_HOLD);
        }
    }

  #if defined(DEBUG_BALANCE_STATE)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    f->state.dsp = f->dsp_orig;
  #endif

    // Eval_Core() expects a varlist to be in the frame, therefore it must
    // be filled in by Reuse_Varlist(), or if this is something like a DO
    // of a FRAME! it needs to be filled in from that frame before eval'ing.
    //
    TRASH_POINTER_IF_DEBUG(f->varlist);
}

inline static void Push_Frame(REBVAL *out, REBFRM *f)
{
    Push_Frame_No_Varlist(out, f);
    Reuse_Varlist_If_Available(f);
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->feed->index; // this is garbage if EVAL_FLAG_VA_LIST
}


// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out to just take a void* (because a C va_list
// cannot have its first parameter in the variadic, va_list* is insufficient)
//
inline static const RELVAL *Detect_Feed_Pointer_Maybe_Fetch(
    struct Reb_Feed *feed,
    const void *p,
    bool preserve
){
    const RELVAL *lookback;

    if (not preserve)
        lookback = nullptr;
    else {
        assert(READABLE(feed->value, __FILE__, __LINE__));  // ensure cell

        if (GET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY)) {
            //
            // f->value was transient and hence constructed into f->fetched.
            // We may overwrite it below for this fetch.  So save the old one
            // into f->lookback, where it will be safe until the next fetch.
            //
            assert(feed->value == &feed->fetched);
            lookback = Move_Value(&feed->lookback, KNOWN(&feed->fetched));
        }
        else {
            // pointer they had should be stable, GC-safe

            lookback = feed->value;
        }
    }

  detect_again:;

    TRASH_POINTER_IF_DEBUG(feed->value);  // should be assigned below

    if (not p) {  // libRebol's null/<opt> (IS_NULLED prohibited in CELL case)

        if (QUOTING_BYTE(feed) == 0)
            panic ("Cannot directly splice nulls...use rebQ(), rebXxxQ()");

        // !!! We could make a global QUOTED_NULLED_VALUE with a stable
        // pointer and not have to use fetched or FETCHED_MARKED_TEMPORARY.
        //
        feed->array = nullptr;
        Quotify(Init_Nulled(&feed->fetched), 1);
        SET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
        feed->value = &feed->fetched;

    } else switch (Detect_Rebol_Pointer(p)) {

      case DETECTED_AS_UTF8: {
        REBDSP dsp_orig = DSP;

        // !!! Current hack is to just allow one binder to be passed in for
        // use binding any newly loaded portions (spliced ones are left with
        // their bindings, though there may be special "binding instructions"
        // or otherwise, that get added).
        //
        feed->context = Get_Context_From_Stack();
        feed->lib = (feed->context != Lib_Context) ? Lib_Context : nullptr;

        struct Reb_Binder binder;
        Init_Interning_Binder(&binder, feed->context);
        feed->binder = &binder;

        feed->specifier = SPECIFIED;

        SCAN_STATE ss;
        const REBLIN start_line = 1;
        Init_Va_Scan_State_Core(
            &ss,
            Intern("sys-do.h"),
            start_line,
            cast(const REBYTE*, p),
            feed
        );

        REBVAL *error = rebRescue(cast(REBDNG*, &Scan_To_Stack), &ss);
        Shutdown_Interning_Binder(&binder, feed->context);

        if (error) {
            REBCTX *error_ctx = VAL_CONTEXT(error);
            rebRelease(error);
            fail (error_ctx);
        }

        // !!! for now, assume scan went to the end; ultimately it would need
        // to pass the feed in as a parameter for partial scans
        //
        feed->vaptr = nullptr;

        if (DSP == dsp_orig) {
            //
            // This happens when somone says rebRun(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in f->value, and it's unknown if the variadic
            // feed is actually over so as to put null... so get another
            // value out of the va_list and keep going.
            //
            p = va_arg(*feed->vaptr, const void*);
            goto detect_again;
        }

        REBARR *reified = Pop_Stack_Values(dsp_orig);

        // !!! We really should be able to free this array without managing it
        // when we're done with it, though that can get a bit complicated if
        // there's an error or need to reify into a value.  For now, do the
        // inefficient thing and manage it.
        //
        // !!! Scans that produce only one value (which are likely very
        // common) can go into feed->fetched and not make an array at all.
        //
        Manage_Array(reified);

        feed->value = ARR_HEAD(reified);
        feed->pending = feed->value + 1;  // may be END
        feed->array = reified;
        feed->index = 1;

        CLEAR_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
        break; }

      case DETECTED_AS_SERIES: {  // e.g. rebQ, rebU, or a rebR() handle
        REBARR *inst1 = ARR(m_cast(void*, p));

        // As we feed forward, we're supposed to be freeing this--it is not
        // managed -and- it's not manuals tracked, it is only held alive by
        // the va_list()'s plan to visit it.  A fail() here won't auto free
        // it *because it is this traversal code which is supposed to free*.
        //
        // !!! Actually, THIS CODE CAN'T FAIL.  :-/  It is part of the
        // implementation of fail's cleanup itself.
        //
        if (GET_ARRAY_FLAG(inst1, INSTRUCTION_ADJUST_QUOTING)) {
            assert(NOT_SERIES_FLAG(inst1, MANAGED));

            if (QUOTING_BYTE(feed) + MISC(inst1).quoting_delta < 0)
                panic ("rebU() can't unquote a feed splicing plain values");

            assert(ARR_LEN(inst1) > 0);
            if (ARR_LEN(inst1) > 1)
                panic ("rebU() of more than one value splice not written");

            REBVAL *single = KNOWN(ARR_SINGLE(inst1));
            Move_Value(&feed->fetched, single);
            Quotify(
                &feed->fetched,
                QUOTING_BYTE(feed) + MISC(inst1).quoting_delta
            );
            SET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
            feed->value = &feed->fetched;

            GC_Kill_Series(SER(inst1));  // not manuals-tracked
        }
        else if (GET_ARRAY_FLAG(inst1, SINGULAR_API_RELEASE)) {
            assert(GET_SERIES_FLAG(inst1, MANAGED));

            // See notes above (duplicate code, fix!) about how we might like
            // to use the as-is value and wait to free until the next cycle
            // vs. putting it in fetched/MARKED_TEMPORARY...but that makes
            // this more convoluted.  Review.

            REBVAL *single = KNOWN(ARR_SINGLE(inst1));
            Move_Value(&feed->fetched, single);
            Quotify(&feed->fetched, QUOTING_BYTE(feed));
            SET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
            feed->value = &feed->fetched;
            rebRelease(cast(const REBVAL*, single));  // *is* the instruction
        }
        else
            panic (inst1);

        break; }

      case DETECTED_AS_CELL: {
        const REBVAL *cell = cast(const REBVAL*, p);
        assert(not IS_RELATIVE(cast(const RELVAL*, cell)));

        feed->array = nullptr;

        if (IS_NULLED(cell))  // API enforces use of C's nullptr (0) for NULL
            assert(!"NULLED cell API leak, see NULLIFY_NULLED() in C source");

        if (QUOTING_BYTE(feed) == 0) {
            feed->value = cell;  // cell can be used as-is
        }
        else {
            // We don't want to corrupt the value itself.  We have to move
            // it into the fetched cell and quote it.
            //
            Quotify(Move_Value(&feed->fetched, cell), QUOTING_BYTE(feed));
            SET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
            feed->value = &feed->fetched;  // note END is detected separately
        }
        break; }

      case DETECTED_AS_END: {  // end of variadic input, so that's it for this
        feed->value = END_NODE;
        TRASH_POINTER_IF_DEBUG(feed->pending);

        // The va_end() is taken care of here, or if there is a throw/fail it
        // is taken care of by Abort_Frame_Core()
        //
        va_end(*feed->vaptr);
        feed->vaptr = nullptr;

        // !!! Error reporting expects there to be an array.  The whole story
        // of errors when there's a va_list is not told very well, and what
        // will have to likely happen is that in debug modes, all va_list
        // are reified from the beginning, else there's not going to be
        // a way to present errors in context.  Fake an empty array for now.
        //
        feed->array = EMPTY_ARRAY;
        feed->index = 0;

        CLEAR_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);  // needed?
        break; }

      case DETECTED_AS_FREED_SERIES:
      case DETECTED_AS_FREED_CELL:
      default:
        panic (p);
    }

    return lookback;
}


//
// Fetch_Next_In_Feed_Core() (see notes above)
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.  f->feed->pending thus
// must hold a signal that data remains in the va_list and it should be
// consulted further.  That signal is an END marker.
//
// More generally, an END marker in f->feed->pending for this routine is a
// signal that the vaptr (if any) should be consulted next.
//
inline static const RELVAL *Fetch_Next_In_Feed_Core(
    struct Reb_Feed *feed,
    bool preserve
){
  #ifdef DEBUG_EXPIRED_LOOKBACK
    if (feed->stress) {
        TRASH_CELL_IF_DEBUG(feed->stress);
        free(feed->stress);
        feed->stress = nullptr;
    }
  #endif

    // We are changing ->value, and thus by definition any ->gotten value
    // will be invalid.  It might be "wasteful" to always set this to null,
    // especially if it's going to be overwritten with the real fetch...but
    // at a source level, having every call to Fetch_Next_In_Frame have to
    // explicitly set ->gotten to null is overkill.  Could be split into
    // a version that just trashes ->gotten in the debug build vs. null.
    //
    feed->gotten = nullptr;

    const RELVAL *lookback;

    if (NOT_END(feed->pending)) {
        //
        // We assume the ->pending value lives in a source array, and can
        // just be incremented since the array has SERIES_INFO_HOLD while it
        // is being executed hence won't be relocated or modified.  This
        // means the release build doesn't need to call ARR_AT().
        //
        assert(
            feed->array // incrementing plain array of REBVAL[]
            or feed->pending == ARR_AT(feed->array, feed->index)
        );

        lookback = feed->value;  // should have been stable
        feed->value = feed->pending;

        ++feed->pending; // might be becoming an END marker, here
        ++feed->index;
    }
    else if (not feed->vaptr) {
        //
        // The frame was either never variadic, or it was but got spooled into
        // an array by Reify_Va_To_Array_In_Frame().  The first END we hit
        // is the full stop end.

        lookback = feed->value;
        feed->value = END_NODE;
        TRASH_POINTER_IF_DEBUG(feed->pending);

        ++feed->index; // for consistency in index termination state

        if (GET_FEED_FLAG(feed, TOOK_HOLD)) {
            assert(GET_SERIES_INFO(feed->array, HOLD));
            CLEAR_SERIES_INFO(feed->array, HOLD);

            // !!! Future features may allow you to move on to another array.
            // If so, the "hold" bit would need to be reset like this.
            //
            CLEAR_FEED_FLAG(feed, TOOK_HOLD);
        }
    }
    else {
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        const void *p = va_arg(*feed->vaptr, const void*);
        feed->index = TRASHED_INDEX; // avoids warning in release build
        lookback = Detect_Feed_Pointer_Maybe_Fetch(feed, p, preserve);
    }

    assert(
        IS_END(feed->value)
        or feed->value == &feed->fetched
        or NOT_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY)
    );

  #ifdef DEBUG_EXPIRED_LOOKBACK
    if (preserve) {
        f->stress = cast(RELVAL*, malloc(sizeof(RELVAL)));
        memcpy(f->stress, *opt_lookback, sizeof(RELVAL));
        lookback = f->stress;
    }
  #endif

    return lookback;
}

#define Fetch_First_In_Feed(feed) \
    Fetch_Next_In_Feed_Core((feed), false)

inline static const RELVAL *Fetch_Next_In_Feed(  // adds not-end checking
    struct Reb_Feed *feed,
    bool preserve
){
    ASSERT_NOT_END(feed->value);
    return Fetch_Next_In_Feed_Core(feed, preserve);
}


// Most calls to Fetch_Next_In_Frame() are no longer interested in the
// cell backing the pointer that used to be in f->value (this is enforced
// by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
// taken when one is interested in that data, because it may have to be
// moved.  So current can be returned from Fetch_Next_In_Frame_Core().

#define Lookback_While_Fetching_Next(f) \
    Fetch_Next_In_Feed(FRM(f)->feed, true)

#define Fetch_Next_Forget_Lookback(f) \
    ((void)Fetch_Next_In_Feed(FRM(f)->feed, false))


// This code is shared by Literal_Next_In_Feed(), and used without a feed
// advancement in the inert branch of the evaluator.  So for something like
// `loop 2 [append [] 10]`, the steps are:
//
//    1. loop defines its body parameter as <const>
//    2. When LOOP runs Do_Any_Array_At_Throws() on the const ARG(body), the
//       frame gets FEED_FLAG_CONST due to the CELL_FLAG_CONST.
//    3. The argument to append is handled by the inert processing branch
//       which moves the value here.  If the block wasn't made explicitly
//       mutable (e.g. with MUTABLE) it takes the flag from the feed.
//
inline static void Inertly_Derelativize_Inheriting_Const(
    REBVAL *out,
    const RELVAL *v,
    struct Reb_Feed *feed
){
    Derelativize(out, v, feed->specifier);
    SET_CELL_FLAG(out, UNEVALUATED);
    if (not GET_CELL_FLAG(v, EXPLICITLY_MUTABLE))
        out->header.bits |= (feed->flags.bits & FEED_FLAG_CONST);
}

inline static void Literal_Next_In_Feed(REBVAL *out, struct Reb_Feed *feed) {
    Inertly_Derelativize_Inheriting_Const(out, feed->value, feed);
    (void)(Fetch_Next_In_Feed(feed, false));
}

#define Literal_Next_In_Frame(out,f) \
    Literal_Next_In_Feed((out), (f)->feed)

inline static void Abort_Frame(REBFRM *f) {
    if (f->varlist and NOT_SERIES_FLAG(f->varlist, MANAGED))
        GC_Kill_Series(SER(f->varlist)); // not alloc'd with manuals tracking
    TRASH_POINTER_IF_DEBUG(f->varlist);

    // Abort_Frame() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    // 
    if (IS_END(f->feed->value))
        goto pop;

    if (FRM_IS_VALIST(f)) {
        assert(NOT_FEED_FLAG(f->feed, TOOK_HOLD));

        // Aborting valist frames is done by just feeding all the values
        // through until the end.  This is assumed to do any work, such
        // as SINGULAR_FLAG_API_RELEASE, which might be needed on an item.  It
        // also ensures that va_end() is called, which happens when the frame
        // manages to feed to the end.
        //
        // Note: While on many platforms va_end() is a no-op, the C standard
        // is clear it must be called...it's undefined behavior to skip it:
        //
        // http://stackoverflow.com/a/32259710/211160

        // !!! Since we're not actually fetching things to run them, this is
        // overkill.  A lighter sweep of the va_list pointers that did just
        // enough work to handle rebR() releases, and va_end()ing the list
        // would be enough.  But for the moment, it's more important to keep
        // all the logic in one place than to make variadic interrupts
        // any faster...they're usually reified into an array anyway, so
        // the frame processing the array will take the other branch.

        while (NOT_END(f->feed->value))
            Fetch_Next_Forget_Lookback(f);
    }
    else {
        if (GET_FEED_FLAG(f->feed, TOOK_HOLD)) {
            //
            // The frame was either never variadic, or it was but got spooled
            // into an array by Reify_Va_To_Array_In_Frame()
            //
            assert(GET_SERIES_INFO(f->feed->array, HOLD));
            CLEAR_SERIES_INFO(f->feed->array, HOLD);
            CLEAR_FEED_FLAG(f->feed, TOOK_HOLD); // !!! needed?
        }
    }

pop:;

    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;
}


inline static void Drop_Frame_Core(REBFRM *f) {
  #if defined(DEBUG_EXPIRED_LOOKBACK)
    free(f->stress);
  #endif

    if (f->varlist) {
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        LINK(f->varlist).reuse = TG_Reuse;
        TG_Reuse = f->varlist;
    }
    TRASH_POINTER_IF_DEBUG(f->varlist);

    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;
}

inline static void Drop_Frame_Unbalanced(REBFRM *f) {
    Drop_Frame_Core(f);
}

inline static void Drop_Frame(REBFRM *f)
{
  #if defined(DEBUG_BALANCE_STATE)
    //
    // To avoid slowing down the debug build a lot, Eval_Core() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    f->state.dsp = DSP; // e.g. Reduce_To_Stack_Throws() doesn't want check
    ASSERT_STATE_BALANCED(&f->state);
  #endif

    assert(DSP == f->dsp_orig); // Drop_Frame_Core() does not check
    Drop_Frame_Unbalanced(f);
}


// It is more pleasant to have a uniform way of speaking of frames by pointer,
// so this macro sets that up for you, the same way DECLARE_LOCAL does.  The
// optimizer should eliminate the extra pointer.
//
// Just to simplify matters, the frame cell is set to a bit pattern the GC
// will accept.  It would need stack preparation anyway, and this simplifies
// the invariant so if a recycle happens before Eval_Core() gets to its
// body, it's always set to something.  Using an unreadable blank means we
// signal to users of the frame that they can't be assured of any particular
// value between evaluations; it's not cleared.
//

inline static void Prep_Array_Feed(
    struct Reb_Feed *feed,
    const RELVAL *opt_first,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    REBFLGS flags
){
    Prep_Stack_Cell(&feed->fetched);
    Init_Unreadable_Blank(&feed->fetched);
    Prep_Stack_Cell(&feed->lookback);
    Init_Unreadable_Blank(&feed->lookback);

    feed->vaptr = nullptr;
    feed->array = array;
    feed->specifier = specifier;
    feed->flags.bits = flags;
    if (opt_first) {
        feed->value = opt_first;
        feed->index = index;
        feed->pending = ARR_AT(array, index);
        ASSERT_NOT_END(feed->value);
    }
    else {
        feed->value = ARR_AT(array, index);
        feed->index = index + 1;
        feed->pending = feed->value + 1;
    }

    feed->gotten = nullptr;
    assert(IS_END(feed->value) or READABLE(feed->value, __FILE__, __LINE__));
}

#define DECLARE_ARRAY_FEED(name,array,index,specifier) \
    struct Reb_Feed name##struct; \
    Prep_Array_Feed(&name##struct, \
        nullptr, (array), (index), (specifier), FEED_MASK_DEFAULT \
    ); \
    struct Reb_Feed *name = &name##struct

inline static void Prep_Va_Feed(
    struct Reb_Feed *feed,
    const void *opt_first,
    va_list *vaptr,
    REBFLGS flags
){
    Prep_Stack_Cell(&feed->fetched);
    Init_Unreadable_Blank(&feed->fetched);
    Prep_Stack_Cell(&feed->lookback);
    Init_Unreadable_Blank(&feed->lookback);

    feed->index = TRASHED_INDEX;  // avoid warning in release build
    feed->array = nullptr;
    feed->flags.bits = flags;
    feed->vaptr = vaptr;
    feed->pending = END_NODE;  // signal next fetch comes from va_list
    feed->specifier = SPECIFIED;  // relative values not allowed
    if (opt_first)
        Detect_Feed_Pointer_Maybe_Fetch(feed, opt_first, false);
    else
        Fetch_First_In_Feed(feed);

    feed->gotten = nullptr;
    assert(IS_END(feed->value) or READABLE(feed->value, __FILE__, __LINE__));
}

// The flags is passed in by the macro here by default, because it does a
// fetch as part of the initialization from the opt_first...and if you want
// FLAG_QUOTING_BYTE() to take effect, it must be passed in up front.
//
#define DECLARE_VA_FEED(name,opt_first,vaptr,flags) \
    struct Reb_Feed name##struct; \
    Prep_Va_Feed(&name##struct, (opt_first), (vaptr), (flags)); \
    struct Reb_Feed *name = &name##struct

inline static void Prep_Any_Array_Feed(
    struct Reb_Feed *feed,
    const RELVAL *any_array,
    REBSPC *specifier,
    REBFLGS parent_flags  // only reads FEED_FLAG_CONST out of this
){
    // Note that `CELL_FLAG_CONST == FEED_FLAG_CONST`
    //
    REBFLGS flags;
    if (GET_CELL_FLAG(any_array, EXPLICITLY_MUTABLE))
        flags = FEED_MASK_DEFAULT;  // override const from parent frame
    else
        flags = FEED_MASK_DEFAULT
            | (parent_flags & FEED_FLAG_CONST)  // inherit
            | (any_array->header.bits & CELL_FLAG_CONST);  // heed

    Prep_Array_Feed(
        feed,
        nullptr,  // opt_first = nullptr, don't inject arbitrary 1st element
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        Derive_Specifier(specifier, any_array),
        flags
    );
}

#define DECLARE_FEED_AT(name,any_array) \
    struct Reb_Feed name##struct; \
    Prep_Any_Array_Feed(&name##struct, \
        (any_array), SPECIFIED, FS_TOP->feed->flags.bits \
    ); \
    struct Reb_Feed *name = &name##struct

#define DECLARE_FEED_AT_CORE(name,any_array,specifier) \
    struct Reb_Feed name##struct; \
    Prep_Any_Array_Feed(&name##struct, \
        (any_array), (specifier), FS_TOP->feed->flags.bits \
    ); \
    struct Reb_Feed *name = &name##struct

inline static void Prep_Frame_Core(
    REBFRM *f,
    struct Reb_Feed *feed,
    REBFLGS flags
){
    assert(NOT_FEED_FLAG(feed, BARRIER_HIT));  // couldn't do anything

    f->feed = feed;
    Prep_Stack_Cell(&f->spare);
    Init_Unreadable_Blank(&f->spare);
    f->dsp_orig = DS_Index;
    f->flags = Endlike_Header(flags);
    TRASH_POINTER_IF_DEBUG(f->out);
}

#define DECLARE_FRAME(name,feed,flags) \
    REBFRM name##struct; \
    Prep_Frame_Core(&name##struct, feed, flags); \
    REBFRM * const name = &name##struct

#define DECLARE_FRAME_AT(name,any_array,flags) \
    DECLARE_FEED_AT (name##feed, any_array); \
    DECLARE_FRAME (name, name##feed, flags)

#define DECLARE_END_FRAME(name,flags) \
    DECLARE_FRAME(name, &TG_Frame_Feed_End, flags)


inline static void Begin_Action(REBFRM *f, REBSTR *opt_label)
{
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));

    assert(not f->original);
    f->original = FRM_PHASE(f);

    assert(IS_POINTER_TRASH_DEBUG(f->opt_label)); // only valid w/REB_ACTION
    assert(not opt_label or GET_SERIES_FLAG(opt_label, IS_UTF8_STRING));
    f->opt_label = opt_label;
  #if defined(DEBUG_FRAME_LABELS) // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    f->refine = ORDINARY_ARG;

    assert(NOT_EVAL_FLAG(f, REQUOTE_NULL));
    f->requotes = 0;
}


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates f->param so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
// when it has to build the frame for the user to write to before running;
// so Eval_Core only checks the arguments, and does not fulfill them.
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
// Rather than "dig" through layers of functions to find the underlying
// function or the specialization's exemplar frame, those properties are
// cached during the creation process.
//
inline static void Push_Action(
    REBFRM *f,
    REBACT *act,
    REBNOD *binding
){
    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));

    f->param = ACT_PARAMS_HEAD(act); // Specializations hide some params...
    REBCNT num_args = ACT_NUM_PARAMS(act); // ...so see REB_TS_HIDDEN

    // !!! Note: Should pick "smart" size when allocating varlist storage due
    // to potential reuse--but use exact size for *this* action, for now.
    //
    REBSER *s;
    if (not f->varlist) { // usually means first action call in the REBFRM
        s = Alloc_Series_Node(
            SERIES_MASK_VARLIST
                | SERIES_FLAG_STACK_LIFETIME
                | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
        );
        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // signals array, also implicit terminator
                | FLAG_LEN_BYTE_OR_255(255) // signals dynamic
        );
        INIT_LINK_KEYSOURCE(s, NOD(f)); // maps varlist back to f
        MISC_META_NODE(s) = nullptr; // GC will sees this
        f->varlist = ARR(s);
    }
    else {
        s = SER(f->varlist);
        if (s->content.dynamic.rest >= num_args + 1 + 1) // +roovar, +end
            goto sufficient_allocation;

        //assert(SER_BIAS(s) == 0);
        Free_Unbiased_Series_Data(
            s->content.dynamic.data,
            SER_TOTAL(s)
        );
    }

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) // +rootvar, +end
        fail ("Out of memory in Push_Action()");

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    f->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | NODE_FLAG_STACK
            | CELL_FLAG_PROTECTED  // payload/binding tweaked, but not by user
            | CELL_MASK_CONTEXT
            | FLAG_KIND_BYTE(REB_FRAME);
    TRACK_CELL_IF_DEBUG(f->rootvar, __FILE__, __LINE__);
    INIT_VAL_CONTEXT_VARLIST(f->rootvar, f->varlist);

  sufficient_allocation:

    INIT_VAL_CONTEXT_PHASE(f->rootvar, act);  // FRM_PHASE() (can be dummy)
    EXTRA(Binding, f->rootvar).node = binding; // FRM_BINDING()

    s->content.dynamic.used = num_args + 1;
    RELVAL *tail = ARR_TAIL(f->varlist);
    tail->header.bits = NODE_FLAG_STACK | FLAG_KIND_BYTE(REB_0);
    TRACK_CELL_IF_DEBUG(tail, __FILE__, __LINE__);

    // Current invariant for all arrays (including fixed size), last cell in
    // the allocation is an end.
    RELVAL *ultimate = ARR_AT(f->varlist, s->content.dynamic.rest - 1);
    ultimate->header = Endlike_Header(0); // unreadable
    TRACK_CELL_IF_DEBUG(ultimate, __FILE__, __LINE__);

  #if !defined(NDEBUG)
    RELVAL *prep = ultimate - 1;
    for (; prep > tail; --prep) {
        prep->header.bits = FLAG_KIND_BYTE(REB_T_TRASH); // unreadable
        TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
    }
  #endif

    f->arg = f->rootvar + 1;

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // f->special here will either equal f->param (to indicate normal argument
    // fulfillment) or the head of the "exemplar".  To speed this up, the
    // absence of a cached exemplar just means that the "specialty" holds the
    // paramlist... this means no conditional code is needed here.
    //
    f->special = ACT_SPECIALTY_HEAD(act);

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(NOT_SERIES_INFO(f->varlist, INACCESSIBLE));

    // There's a current state for the FEED_FLAG_NO_LOOKAHEAD which invisible
    // actions want to put back as it was when the invisible operation ends.
    // (It gets overwritten during the invisible's own argument gathering).
    // Cache it on the varlist and put it back when an R_INVISIBLE result
    // comes back.
    //
    // !!! Should this go in Begin_Action?
    //
    if (GET_ACTION_FLAG(act, IS_INVISIBLE)) {
        if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
            assert(GET_EVAL_FLAG(f, FULFILLING_ARG));
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            SET_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD);
        }
    }
}


inline static void Drop_Action(REBFRM *f) {
    assert(NOT_SERIES_FLAG(f->varlist, VARLIST_FRAME_FAILED));

    assert(
        not f->opt_label
        or GET_SERIES_FLAG(f->opt_label, IS_UTF8_STRING)
    );

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);

    CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);
    CLEAR_EVAL_FLAG(f, FULFILL_ONLY);
    CLEAR_EVAL_FLAG(f, REQUOTE_NULL);

    assert(
        GET_SERIES_INFO(f->varlist, INACCESSIBLE)
        or LINK_KEYSOURCE(f->varlist) == NOD(f)
    );

    if (GET_SERIES_INFO(f->varlist, INACCESSIBLE)) {
        //
        // If something like Encloser_Dispatcher() runs, it might steal the
        // variables from a context to give them to the user, leaving behind
        // a non-dynamic node.  Pretty much all the bits in the node are
        // therefore useless.  It served a purpose by being non-null during
        // the call, however, up to this moment.
        //
        if (GET_SERIES_FLAG(f->varlist, MANAGED))
            f->varlist = nullptr; // references exist, let a new one alloc
        else {
            // This node could be reused vs. calling Make_Node() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Node(SER_POOL, NOD(f->varlist));
            f->varlist = nullptr;
        }
    }
    else if (GET_SERIES_FLAG(f->varlist, MANAGED)) {
        //
        // The varlist wound up getting referenced in a cell that will outlive
        // this Drop_Action().  The pointer needed to stay working up until
        // now, but the args memory won't be available.  But since we know
        // there were outstanding references to the varlist, we need to
        // convert it into a "stub" that's enough to avoid crashes.
        //
        // ...but we don't free the memory for the args, we just hide it from
        // the stub and get it ready for potential reuse by the next action
        // call.  That's done by making an adjusted copy of the stub, which
        // steals its dynamic memory (by setting the stub not HAS_DYNAMIC).
        //
        f->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                CTX(f->varlist),
                NOD(f->original) // degrade keysource from f
            )
        );
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        INIT_LINK_KEYSOURCE(f->varlist, NOD(f));
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.  
        //
        // But no series bits we didn't set should be set...and right now,
        // only Enter_Native() sets HOLD.  Clear that.  Also, it's possible
        // for a "telegraphed" no lookahead bit used by an invisible to be
        // left on, so clear it too.
        //
        CLEAR_SERIES_INFO(f->varlist, HOLD);
        CLEAR_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD);

        assert(0 == (SER(f->varlist)->info.bits & ~( // <- note bitwise not
            SERIES_INFO_0_IS_TRUE // parallels NODE_FLAG_NODE
            | FLAG_WIDE_BYTE_OR_0(0) // don't mask out wide (0 for arrays))
            | FLAG_LEN_BYTE_OR_255(255) // mask out non-dynamic-len (dynamic)
        )));
    }

  #if !defined(NDEBUG)
    if (f->varlist) {
        assert(NOT_SERIES_INFO(f->varlist, INACCESSIBLE));
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

        RELVAL *rootvar = ARR_HEAD(f->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == f->varlist);
        TRASH_POINTER_IF_DEBUG(PAYLOAD(Any, rootvar).second.node);  // phase
        TRASH_POINTER_IF_DEBUG(EXTRA(Binding, rootvar).node);
    }
  #endif

    f->original = nullptr; // signal an action is no longer running

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ARGUMENT AND PARAMETER ACCESS HELPERS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Reb_Frame* passed to every
// REBNATIVE ('frame_') and read the information out cleanly, like this:
//
//     PARAM(1, foo);
//     REFINE(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) and REF(bar)) { ... }
//
// Though REF can only be used with a REFINE() declaration, ARG can be used
// with either.  By contract, Rebol functions are allowed to mutate their
// arguments and refinements just as if they were locals...guaranteeing only
// their return result as externally visible.  Hence the ARG() cell for a
// refinement provides a GC-safe slot for natives to hold values once they
// have observed what they need from the refinement.
//
// Under the hood `PARAM(1, foo)` and `REFINE(2, bar)` are const values in
// the release build.  Under optimization they disappear completely, so that
// addressing is done directly into the call frame's cached `arg` pointer.
// It is also possible to get the typeset-with-symbol for a particular
// parameter or refinement, e.g. with `PAR(foo)` or `PAR(bar)`.
//
// The PARAM and REFINE macros use token pasting to name the variables they
// are declaring `p_name` instead of just `name`.  This prevents collisions
// with C/C++ identifiers, so PARAM(case) and REFINE(new) would make `p_case`
// and `p_new` instead of just `case` and `new` as the variable names.  (This
// is only visible in the debugger.)
//
// As a further aid, the debug build version of the structures contain the
// actual pointers to the arguments.  It also keeps a copy of a cache of the
// type for the arguments, because the numeric type encoding in the bits of
// the header requires a debug call (or by-hand-binary decoding) to interpret
// Whether a refinement was used or not at time of call is also cached.
//

#ifdef NDEBUG
    #define PARAM(n,name) \
        static const int p_##name = n

    #define REFINE(n,name) \
        static const int p_##name = n

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name))

    #define PAR(name) \
        ACT_PARAM(FRM_PHASE(frame_), (p_##name))  // a REB_P_XXX pseudovalue

    #define REF(name) \
        (not IS_BLANK(ARG(name)))  // should be faster than IS_FALSEY()
#else
    struct Native_Param {
        int num;
        enum Reb_Kind kind_cache; // for inspecting in watchlist
        REBVAL *arg; // for inspecting in watchlist
    };

    struct Native_Refine {
        int num;
        bool used_cache; // for inspecting in watchlist
        REBVAL *arg; // for inspecting in watchlist
    };

    // Note: Assigning non-const initializers to structs, e.g. `= {var, f()};`
    // is a non-standard extension to C.  So we break out the assignments.

    #define PARAM(n,name) \
        struct Native_Param p_##name; \
        p_##name.num = (n); \
        p_##name.kind_cache = VAL_TYPE(FRM_ARG(frame_, (n))); \
        p_##name.arg = FRM_ARG(frame_, (n)); \

    #define REFINE(n,name) \
        struct Native_Refine p_##name; \
        p_##name.num = (n); \
        p_##name.used_cache = IS_TRUTHY(FRM_ARG(frame_, (n))); \
        p_##name.arg = FRM_ARG(frame_, (n)); \

    #define ARG(name) \
        FRM_ARG(frame_, (p_##name).num)

    #define PAR(name) \
        ACT_PARAM(FRM_PHASE(frame_), (p_##name).num) /* a TYPESET! */

    #define REF(name) \
        ((p_##name).used_cache /* used_cache use stops REF() on PARAM()s */ \
            ? not IS_BLANK(ARG(name)) \
            : not IS_BLANK(ARG(name)))
#endif

// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_FRAME     frame_
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_ARGC      FRM_NUM_ARGS(frame_)    // count of args+refinements/args
#define D_ARG(n)    FRM_ARG(frame_, (n))    // pass 1 for first arg
#define D_SPARE     FRM_SPARE(frame_)       // scratch GC-safe cell

#define RETURN(v) \
    return Move_Value(D_OUT, (v));


// The native entry prelude makes sure that once native code starts running,
// then the frame's stub is flagged to indicate access via a FRAME! should
// not have write access to variables.  That could cause crashes, as raw C
// code is not insulated against having bit patterns for types in cells that
// aren't expected.
//
// !!! Debug injection of bad types into usermode code may cause havoc as
// well, and should be considered a security/permissions issue.  It just won't
// (or shouldn't) crash the evaluator itself.
//
// This is automatically injected by the INCLUDE_PARAMS_OF_XXX macros.  The
// reason this is done with code inlined into the native itself instead of
// based on an IS_NATIVE() test is to avoid the cost of the testing--which
// is itself a bit dodgy to tell a priori if a dispatcher is native or not.
// This way there is no test and only natives pay the cost of flag setting.
//
inline static void Enter_Native(REBFRM *f) {
    SET_SERIES_INFO(f->varlist, HOLD); // may or may not be managed
}
