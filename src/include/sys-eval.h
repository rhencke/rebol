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


// !!! Find a better place for this!
//
inline static bool IS_QUOTABLY_SOFT(const RELVAL *v) {
    return IS_GROUP(v) or IS_GET_WORD(v) or IS_GET_PATH(v);
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
//

inline static void Push_Frame_Core(REBFRM *f)
{
    // All calls to a Eval_Core_Throws() are assumed to happen at the same C
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
        if (Is_Action_Frame_Fulfilling(ftemp))
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

    TRASH_POINTER_IF_DEBUG(f->varlist); // must Try_Reuse_Varlist() or fill in

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
}

// Pretend the input source has ended; used with REB_E_PROCESS_ACTION.
//
inline static void Push_Frame_At_End(REBFRM *f, REBFLGS flags) {
    f->flags = Endlike_Header(flags);

    assert(f->feed == &TG_Frame_Feed_End); // see DECLARE_END_FRAME
    assert(f->feed->gotten == nullptr);
    f->feed->value = END_NODE;
    f->feed->specifier = SPECIFIED;

    Push_Frame_Core(f);
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->feed->index; // this is garbage if EVAL_FLAG_VA_LIST
}

inline static void Reuse_Varlist_If_Available(REBFRM *f) {
    assert(IS_POINTER_TRASH_DEBUG(f->varlist));
    if (not TG_Reuse)
        f->varlist = nullptr;
    else {
        f->varlist = TG_Reuse;
        TG_Reuse = LINK(TG_Reuse).reuse;
        f->rootvar = cast(REBVAL*, SER(f->varlist)->content.dynamic.data);
        LINK(f->varlist).keysource = NOD(f);
    }
}

inline static void Push_Frame_At(
    REBFRM *f,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    REBFLGS flags
){
    f->flags = Endlike_Header(flags);

    f->feed->value = ARR_AT(array, index);

    f->feed->vaptr = nullptr;
    f->feed->array = array;
    f->feed->flags.bits = FEED_MASK_DEFAULT;
    f->feed->index = index + 1;
    f->feed->pending = f->feed->value + 1;
    assert(f->feed->gotten == nullptr);  // DECLARE_FRAME()/etc. sets

    f->feed->specifier = specifier;

    // Frames are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use an unwritable END until the first eval call.
    //
    f->out = m_cast(REBVAL*, END_NODE);

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
}

inline static void Push_Frame(REBFRM *f, const REBVAL *v)
{
    Push_Frame_At(
        f, VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), EVAL_MASK_DEFAULT
    );
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

        if (GET_FEED_FLAG(feed, UNEVALUATIVE))
            fail ("rebUNEVALUATIVE/rebU API mode cannot splice nulls");

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
        MANAGE_ARRAY(reified);

        feed->value = ARR_HEAD(reified);
        feed->pending = feed->value + 1;  // may be END
        feed->array = reified;
        feed->index = 1;

        CLEAR_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
        break; }

      case DETECTED_AS_SERIES: {  // e.g. rebEVAL(), or a rebR() handle
        REBARR *inst1 = ARR(m_cast(void*, p));

        // The instruction should be unmanaged, and will be freed on the next
        // entry to this routine (optionally copying out its contents into
        // the frame's cell for stable lookback--if necessary).
        //
        if (GET_ARRAY_FLAG(inst1, SINGULAR_API_INSTRUCTION)) {
            assert(NOT_SERIES_FLAG(inst1, MANAGED));

            switch (MISC(inst1).opcode) {
              case API_OPCODE_EVAL: {

                Free_Instruction(inst1);
                TRASH_POINTER_IF_DEBUG(inst1);

                p = va_arg(*feed->vaptr, const void*);
                if (not p)
                    fail ("rebEVAL and rebU/rebUNEVALUATIVE can't take null");

                switch (Detect_Rebol_Pointer(p)) {
                  case DETECTED_AS_CELL: {  // should not be relative
                    feed->value = KNOWN(cast(const REBVAL*, p));
                    feed->index = TRASHED_INDEX;  // necessary?

                    CLEAR_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
                    break; }

                  case DETECTED_AS_SERIES: {
                    //
                    // We allow `rebRun(..., rebEVAL, rebR(v), ...)`
                    //
                    REBARR *inst2 = ARR(m_cast(void*, p));
                    if (
                        GET_ARRAY_FLAG(inst2, SINGULAR_API_INSTRUCTION)
                        or NOT_ARRAY_FLAG(inst2, SINGULAR_API_RELEASE)
                    ){
                        goto not_supported;
                    }

                    // We're freeing the value, so even though it has the
                    // right non-quoted bit pattern, we copy it.  (Previous
                    // attempts to avoid copying and releasing on the *next*
                    // fetch were too convoluted to be worth it, reconsider
                    // if a tidy approach can be done.
                    //
                    // !!! Repeats code below with tiny deviation (no quote)
                    //
                    REBVAL *single = KNOWN(ARR_SINGLE(inst2));
                    Move_Value(&feed->fetched, single);
                    SET_CELL_FLAG(&feed->fetched, FETCHED_MARKED_TEMPORARY);
                    feed->value = &feed->fetched;
                    rebRelease(cast(const REBVAL*, single));
                    break; }

                  default:
                  not_supported:
                    fail ("rebEVAL and rebUNEVALUATIVE/rebU only on REBVAL*");
                }

                break; }

              default:
                panic (p);
            }
        }
        else if (GET_ARRAY_FLAG(inst1, SINGULAR_API_RELEASE)) {
            assert(GET_SERIES_FLAG(inst1, MANAGED));

            REBVAL *single = KNOWN(ARR_SINGLE(inst1));
            if (GET_FEED_FLAG(feed, UNEVALUATIVE)) {
                //
                // See notes above (duplicate code, fix!) about how if we
                // aren't adding a quote, then we might like to use the
                // as-is value and wait to free until the next cycle vs.
                // putting it in fetched/MARKED_TEMPORARY...but that makes
                // this more convoluted.  Review.
                //
                Move_Value(&feed->fetched, single);
            }
            else
                Quotify(Move_Value(&feed->fetched, single), 1);

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

        if (GET_FEED_FLAG(feed, UNEVALUATIVE)) {
            feed->value = cell;  // non-nulled cell can be used as-is
        }
        else {
            // Cells that do not have rebEVAL() preceding them need to appear
            // at one quote level to the evaluator, so that they seem to have
            // already been evaluated (e.g. the lookup by C name counts as
            // their "evaluation", as if they'd been fetched by a WORD!).
            // But we don't want to corrupt the value itself.  We have to move
            // it into the fetched cell and quote it.
            //
            Quotify(Move_Value(&feed->fetched, cell), 1);
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
// Fetch_Next_In_Frame_Core() (see notes above)
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.  f->feed->pending thus
// must hold a signal that data remains in the va_list and it should be
// consulted further.  That signal is an END marker.
//
// More generally, an END marker in f->feed->pending for this routine is a
// signal that the vaptr (if any) should be consulted next.
//
inline static const RELVAL *Fetch_Next_In_Feed(
    struct Reb_Feed *feed,
    bool preserve
){
    assert(NOT_END(feed->value));  // caller should test this first

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


// Most calls to Fetch_Next_In_Frame() are no longer interested in the
// cell backing the pointer that used to be in f->value (this is enforced
// by a rigorous test in DEBUG_EXPIRED_LOOKBACK).  Special care must be
// taken when one is interested in that data, because it may have to be
// moved.  So current can be returned from Fetch_Next_In_Frame_Core().

#define Lookback_While_Fetching_Next(f) \
    Fetch_Next_In_Feed(FRM(f)->feed, true)

#define Fetch_Next_Forget_Lookback(f) \
    ((void)Fetch_Next_In_Feed(FRM(f)->feed, false))


inline static void Literal_Next_In_Frame(REBVAL *dest, REBFRM *f) {
    Derelativize(dest, f->feed->value, f->feed->specifier);
    SET_CELL_FLAG(dest, UNEVALUATED);

    // SEE ALSO: The `inert:` branch in %c-eval.c, which is similar.  We
    // want `append '(a b c) 'd` to be an error, which means the quoting
    // has to get the const flag if intended.
    //
    dest->header.bits |= (f->flags.bits & EVAL_FLAG_CONST);

    Fetch_Next_Forget_Lookback(f);
}


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
  #if defined(DEBUG_BALANCE_STATE)
    //
    // To avoid slowing down the debug build a lot, Eval_Core_Throws() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    f->state.dsp = DSP; // e.g. Reduce_To_Stack_Throws() doesn't want check
    f->state.mold_buf_len = SER_LEN(MOLD_BUF); // REMOVE-EACH accumulates
    ASSERT_STATE_BALANCED(&f->state);
  #endif
    Drop_Frame_Core(f);
}

inline static void Drop_Frame(REBFRM *f)
{
    if (GET_EVAL_FLAG(f, TO_END))
        assert(IS_END(f->feed->value) or Is_Evaluator_Throwing_Debug());

    assert(DSP == f->dsp_orig); // Drop_Frame_Core() does not check
    Drop_Frame_Unbalanced(f);
}


// This is a very light wrapper over Eval_Core_Throws(), which is used with
// Push_Frame_At() for operations like ANY or REDUCE that wish to perform
// several successive operations on an array, without creating a new frame
// each time.
//
inline static bool Eval_Step_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(IS_END(out));

    assert(NOT_EVAL_FLAG(f, TO_END));
    assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));
    assert(NOT_FEED_FLAG(f->feed, BARRIER_HIT));

    f->out = out;
    f->dsp_orig = DSP;
    bool threw = (*PG_Eval_Throws)(f); // should already be pushed

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
    REBFLGS flags,
    REBFRM *child // passed w/dsp_orig preload, refinements can be on stack
){
    assert(NOT_FEED_FLAG(child->feed, BARRIER_HIT));

    child->out = out;

  #if !defined(NDEBUG)
    REBCNT old_index = child->feed->index;
  #endif

    child->flags = Endlike_Header(flags);

    Push_Frame_Core(child);
    Reuse_Varlist_If_Available(child);
    bool threw = (*PG_Eval_Throws)(child);
    Drop_Frame(child);

    assert(
        IS_END(child->feed->value)
        or FRM_IS_VALIST(child)
        or old_index != child->feed->index
        or (flags & EVAL_FLAG_REEVALUATE_CELL)
        or (flags & EVAL_FLAG_POST_SWITCH)
        or Is_Evaluator_Throwing_Debug()
    );

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
    DECLARE_FRAME (f);
    f->flags = Endlike_Header(flags);

    f->feed->vaptr = nullptr;
    f->feed->array = array;
    f->feed->flags.bits = FEED_MASK_DEFAULT;
    assert(f->feed->gotten == nullptr);

    if (opt_first) {
        f->feed->value = opt_first;
        f->feed->index = index;
        f->feed->pending = ARR_AT(array, index);
        assert(NOT_END(f->feed->value));
    }
    else {
        f->feed->value = ARR_AT(array, index);
        f->feed->index = index + 1;
        f->feed->pending = f->feed->value + 1;
        if (IS_END(f->feed->value))
            return END_FLAG;
    }

    f->out = out;
    f->feed->specifier = specifier;

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
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
    DECLARE_FRAME (f);
    f->flags = Endlike_Header(flags); // read by Set_Frame_Detected_Fetch

    f->feed->index = TRASHED_INDEX; // avoids warning in release build
    f->feed->array = nullptr;
    f->feed->flags.bits = FEED_MASK_DEFAULT; // see Reify_Va_To_Array_In_Frame
    f->feed->vaptr = vaptr;
    f->feed->pending = END_NODE; // signal next fetch comes from va_list
    assert(f->feed->gotten == nullptr);

    if (opt_first)
        Detect_Feed_Pointer_Maybe_Fetch(f->feed, opt_first, false);
    else
        Fetch_Next_Forget_Lookback(f);

    if (IS_END(f->feed->value))
        return END_FLAG;

    f->out = out;
    f->feed->specifier = SPECIFIED; // relative values not allowed in va_lists

    Push_Frame_Core(f);
    Reuse_Varlist_If_Available(f);
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
