//
//  File: %n-loop.c
//  Summary: "native functions for loops"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

#include "sys-core.h"
#include "sys-int-funcs.h" //REB_I64_ADD_OF

typedef enum {
    LOOP_FOR_EACH,
    LOOP_MAP_EACH
    // See LOOP_EVERY note: https://github.com/metaeducation/ren-c/issues/847
} LOOP_MODE;


//
//  Catching_Break_Or_Continue: C
//
// Determines if a thrown value is either a break or continue.  If so, `val`
// is mutated to become the throw's argument.  Sets `stop` flag if BREAK.
//
// Returning false means the throw was neither BREAK nor CONTINUE.
//
bool Catching_Break_Or_Continue(REBVAL *val, bool *stop)
{
    assert(THROWN(val));

    // Throw /NAME-s used by CONTINUE and BREAK are the actual native
    // function values of the routines themselves.
    if (not IS_ACTION(val))
        return false;

    if (VAL_ACT_DISPATCHER(val) == &N_break) {
        *stop = true; // was BREAK or BREAK/WITH
        CATCH_THROWN(val, val); // will be void if no /WITH was used
        return true;
    }

    if (VAL_ACT_DISPATCHER(val) == &N_continue) {
        *stop = false; // was CONTINUE or CONTINUE/WITH
        CATCH_THROWN(val, val); // will be void if no /WITH was used
        return true;
    }

    return false; // caller should let all other thrown values bubble up
}


//
//  break: native [
//
//  {Exit the current iteration of a loop and stop iterating further.}
//
//      /with
//          {Act as if loop body finished current evaluation with a value}
//      value [any-value!]
//  ]
//
REBNATIVE(break)
//
// BREAK is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :break`.
{
    INCLUDE_PARAMS_OF_BREAK;

    Move_Value(D_OUT, NAT_VALUE(break));

    UNUSED(REF(with)); // value will be void if no refinement provided
    CONVERT_NAME_TO_THROWN(D_OUT, ARG(value));

    return D_OUT;
}


//
//  continue: native [
//
//  "Throws control back to top of loop for next iteration."
//
//      /with
//          {Act as if loop body finished current evaluation with a value}
//      value [any-value!]
//  ]
//
REBNATIVE(continue)
//
// CONTINUE is implemented via a THROWN() value that bubbles up through
// the stack.  It uses the value of its own native function as the
// name of the throw, like `throw/name value :continue`.
{
    INCLUDE_PARAMS_OF_CONTINUE;

    Move_Value(D_OUT, NAT_VALUE(continue));

    UNUSED(REF(with)); // value will be void if no refinement provided
    CONVERT_NAME_TO_THROWN(D_OUT, ARG(value));

    return D_OUT;
}


//
//  Loop_Series_Common: C
//
static const REBVAL *Loop_Series_Common(
    REBVAL *out,
    REBVAL *var, // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBVAL *start,
    REBINT end,
    REBINT bump
){
    Init_Void(out); // result if body never runs

    // !!! This bounds incoming `end` inside the array.  Should it assert?
    //
    if (end >= cast(REBINT, VAL_LEN_HEAD(start)))
        end = cast(REBINT, VAL_LEN_HEAD(start));
    if (end < 0)
        end = 0;

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to another series, or non-series.
    //
    Move_Value(var, start);
    REBCNT *state = &VAL_INDEX(var);

    // Run only once if start is equal to end...edge case.
    //
    REBINT s = VAL_INDEX(start);
    if (s == end) {
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        return Voidify_If_Nulled(out); // null is reserved for BREAK
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (s < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return out; // avoid infinite loops

    while (
        counting_up
            ? cast(REBINT, *state) <= end
            : cast(REBINT, *state) >= end
    ){
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        Voidify_If_Nulled(out); // null is reserved for BREAK

        if (
            VAL_TYPE(var) != VAL_TYPE(start)
            or VAL_SERIES(var) != VAL_SERIES(start)
        ){
            fail ("Can only change series index, not series to iterate");
        }

        // Note that since the array is not locked with SERIES_INFO_HOLD, it
        // can be mutated during the loop body, so the end has to be refreshed
        // on each iteration.  Review ramifications of HOLD-ing it.
        //
        if (end >= cast(REBINT, VAL_LEN_HEAD(start)))
            end = cast(REBINT, VAL_LEN_HEAD(start));

        *state += bump;
    }

    return out;
}


//
//  Loop_Integer_Common: C
//
static const REBVAL *Loop_Integer_Common(
    REBVAL *out,
    REBVAL *var, // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBI64 start,
    REBI64 end,
    REBI64 bump
){
    Init_Void(out); // result if body never runs

    // A value cell exposed to the user is used to hold the state.  This means
    // if they change `var` during the loop, it affects the iteration.  Hence
    // it must be checked for changing to a non-integer form.
    //
    RESET_VAL_HEADER(var, REB_INTEGER);
    REBI64 *state = &VAL_INT64(var);
    *state = start;

    // Run only once if start is equal to end...edge case.
    //
    if (start == end) {
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        return Voidify_If_Nulled(out); // null is reserved for BREAK
    }

    // As per #1993, start relative to end determines the "direction" of the
    // FOR loop.  (R3-Alpha used the sign of the bump, which meant it did not
    // have a clear plan for what to do with 0.)
    //
    const bool counting_up = (start < end); // equal checked above
    if ((counting_up and bump <= 0) or (not counting_up and bump >= 0))
        return nullptr; // avoid infinite loops

    while (counting_up ? *state <= end : *state >= end) {
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        Voidify_If_Nulled(out); // null is reserved for BREAK

        if (not IS_INTEGER(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        if (REB_I64_ADD_OF(*state, bump, state))
            fail (Error_Overflow_Raw());
    }

    return out;
}


//
//  Loop_Number_Common: C
//
static const REBVAL *Loop_Number_Common(
    REBVAL *out,
    REBVAL *var, // Must not be movable from context expansion, see #2274
    const REBVAL *body,
    REBVAL *start,
    REBVAL *end,
    REBVAL *bump
){
    Init_Void(out); // result if body never runs

    REBDEC s;
    if (IS_INTEGER(start))
        s = cast(REBDEC, VAL_INT64(start));
    else if (IS_DECIMAL(start) or IS_PERCENT(start))
        s = VAL_DECIMAL(start);
    else
        fail (Error_Invalid(start));

    REBDEC e;
    if (IS_INTEGER(end))
        e = cast(REBDEC, VAL_INT64(end));
    else if (IS_DECIMAL(end) or IS_PERCENT(end))
        e = VAL_DECIMAL(end);
    else
        fail (Error_Invalid(end));

    REBDEC b;
    if (IS_INTEGER(bump))
        b = cast(REBDEC, VAL_INT64(bump));
    else if (IS_DECIMAL(bump) or IS_PERCENT(bump))
        b = VAL_DECIMAL(bump);
    else
        fail (Error_Invalid(bump));

    // As in Loop_Integer_Common(), the state is actually in a cell; so each
    // loop iteration it must be checked to ensure it's still a decimal...
    //
    RESET_VAL_HEADER(var, REB_DECIMAL);
    REBDEC *state = &VAL_DECIMAL(var);
    *state = s;

    // Run only once if start is equal to end...edge case.
    //
    if (s == e) {
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        return Voidify_If_Nulled(out); // null is reserved for BREAK
    }

    // As per #1993, see notes in Loop_Integer_Common()
    //
    const bool counting_up = (s < e); // equal checked above
    if ((counting_up and b <= 0) or (not counting_up and b >= 0))
        return Init_Void(out); // avoid infinite loop, void if body never runs

    while (counting_up ? *state <= e : *state >= e) {
        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return out;
            if (stop)
                return nullptr;
        }
        Voidify_If_Nulled(out); // null is reserved for BREAK

        if (not IS_DECIMAL(var))
            fail (Error_Invalid_Type(VAL_TYPE(var)));

        *state += b;
    }

    return out;
}


//
//  Loop_Each: C
//
// Common implementation code of FOR-EACH, MAP-EACH, and EVERY.
//
// !!! This routine has been slowly clarifying since R3-Alpha, and can
// likely be factored in a better way...pushing more per-native code into the
// natives themselves.
//
static const REBVAL *Loop_Each(REBFRM *frame_, LOOP_MODE mode)
{
    INCLUDE_PARAMS_OF_FOR_EACH;

    REBVAL *data = ARG(data);
    assert(not IS_NULLED(data));

    if (IS_BLANK(data))
        return nullptr; // blank in, void out (same result as BREAK)

    bool stop = false;
    bool threw = false; // did a non-BREAK or non-CONTINUE throw occur

    Init_Void(D_OUT); // result if body never runs (MAP-EACH gives [])

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &context,
        ARG(vars)
    );
    Init_Object(ARG(vars), context); // keep GC safe

    // Currently the data stack is only used by MAP-EACH to accumulate results
    // but it's faster to just save it than test the loop mode.
    //
    REBDSP dsp_orig = DSP;

    // Extract the series and index being enumerated, based on data type

    REBSER *series;
    REBCNT index;
    if (ANY_SERIES(data)) {
        series = VAL_SERIES(data);
        index = VAL_INDEX(data);
        if (index >= SER_LEN(series)) {
            if (mode == LOOP_MAP_EACH)
                return Init_Block(D_OUT, Make_Array(0));
            return D_OUT;
        }
    }
    else if (ANY_CONTEXT(data)) {
        series = SER(CTX_VARLIST(VAL_CONTEXT(data)));
        index = 1;
    }
    else if (IS_MAP(data)) {
        series = VAL_SERIES(data);
        index = 0;
    }
    else if (IS_DATATYPE(data)) {
        //
        // !!! Snapshotting the state is not particularly efficient.
        // However, bulletproofing an enumeration of the system against
        // possible GC would be difficult.  And this is really just a
        // debug/instrumentation feature anyway.
        //
        switch (VAL_TYPE_KIND(data)) {
        case REB_ACTION:
            series = SER(Snapshot_All_Actions());
            assert(NOT_SER_FLAG(series, NODE_FLAG_MANAGED)); // content marked
            index = 0;
            break;

        default:
            fail ("ACTION! is the only type with global enumeration");
        }
    }
    else
        panic ("Illegal type passed to Loop_Each()");

    // Iterate over each value in the data series block:

    REBCNT tail;
    while (index < (tail = SER_LEN(series))) {
        REBCNT i;
        REBCNT j = 0;

        REBVAL *key = CTX_KEY(context, 1);
        REBVAL *pseudo_var = CTX_VAR(context, 1);

        // Set the FOREACH loop variables from the series:
        for (i = 1; NOT_END(key); i++, key++, pseudo_var++) {
            //
            // The "var" might have come from a LIT-WORD!, which means it
            // wants us to write into an existing variable.  Note that since
            // these variables are fetched across running arbitrary user
            // code, the address cannot be cached...e.g. the object it lives
            // in might expand and invalidate the location.  (The `context`
            // for fabricated variables is locked at fixed size.)
            //
            REBVAL *var;
            if (GET_VAL_FLAG(pseudo_var, NODE_FLAG_MARKED)) {
                assert(IS_LIT_WORD(pseudo_var));
                var = Get_Mutable_Var_May_Fail(pseudo_var, SPECIFIED);
            } else
                var = pseudo_var;

            if (index >= tail) {
                Init_Nulled(var);
                continue;
            }

            if (ANY_ARRAY(data)) {
                Derelativize(
                    var,
                    ARR_AT(ARR(series), index),
                    VAL_SPECIFIER(data) // !!! always matches series?
                );
            }
            else if (IS_DATATYPE(data)) {
                Derelativize(
                    var,
                    ARR_AT(ARR(series), index),
                    SPECIFIED // array generated via data stack, all specific
                );
            }
            else if (ANY_CONTEXT(data)) {
                if (Is_Param_Hidden(VAL_CONTEXT_KEY(data, index))) {
                    // Do not evaluate this iteration
                    index++;
                    goto skip_hidden;
                }

                // Alternate between word and value parts of object:
                if (j == 0) {
                    Init_Any_Word_Bound(
                        var,
                        REB_WORD,
                        CTX_KEY_SPELLING(VAL_CONTEXT(data), index),
                        CTX(series),
                        index
                    );
                    if (NOT_END(var + 1)) {
                        // reset index for the value part
                        index--;
                    }
                }
                else if (j == 1) {
                    Derelativize(
                        var,
                        ARR_AT(ARR(series), index),
                        SPECIFIED // !!! it's a varlist
                    );
                }
                else {
                    // !!! Review this error (and this routine...)
                    DECLARE_LOCAL (key_name);
                    Init_Word(key_name, VAL_KEY_SPELLING(key));

                    fail (Error_Invalid(key_name));
                }
                j++;
            }
            else if (IS_VECTOR(data)) {
                Get_Vector_At(var, series, index);
            }
            else if (IS_MAP(data)) {
                //
                // MAP! does not store RELVALs
                //
                REBVAL *val = KNOWN(ARR_AT(ARR(series), index | 1));
                if (not IS_NULLED(val)) {
                    if (j == 0) {
                        Derelativize(
                            var,
                            ARR_AT(ARR(series), index & ~1),
                            SPECIFIED // maps always specified
                        );

                        if (IS_END(var + 1)) index++; // only words
                    }
                    else if (j == 1) {
                        Derelativize(
                            var,
                            ARR_AT(ARR(series), index),
                            SPECIFIED // maps always specified
                        );
                    }
                    else {
                        // !!! Review this error (and this routine...)
                        DECLARE_LOCAL (key_name);
                        Init_Word(key_name, VAL_KEY_SPELLING(key));

                        fail (Error_Invalid(key_name));
                    }
                    j++;
                }
                else {
                    index += 2;
                    goto skip_hidden;
                }
            }
            else if (IS_BINARY(data)) {
                Init_Integer(var, (REBI64)(BIN_HEAD(series)[index]));
            }
            else if (IS_IMAGE(data)) {
                Set_Tuple_Pixel(BIN_AT(series, index), var);
            }
            else {
                assert(ANY_STRING(data));
                Init_Char(var, GET_ANY_CHAR(series, index));
            }
            index++;
        }

        assert(IS_END(key) and IS_END(pseudo_var));

        if (Do_Branch_Throws(D_OUT, ARG(body))) {
            if (not Catching_Break_Or_Continue(D_OUT, &stop)) {
                // A non-loop throw, we should be bubbling up
                threw = true;
                break;
            }

            // Fall through and process the D_OUT (unset if no /WITH) for
            // this iteration.  `stop` flag will be checked ater that.
        }

        switch (mode) {
        case LOOP_FOR_EACH:
            Voidify_If_Nulled(D_OUT); // null is reserved for BREAK
            break;

        case LOOP_MAP_EACH:
            // anything that's not null will be added to the result
            if (not IS_NULLED(D_OUT))
                DS_PUSH(D_OUT);
            break;
        }

        if (stop) {
            Init_Nulled(D_OUT);
            break;
        }

skip_hidden: ;
    }

    if (IS_DATATYPE(data))
        Free_Unmanaged_Array(ARR(series)); // temporary array of all instances

    if (threw) {
        // a non-BREAK and non-CONTINUE throw overrides any other return
        // result we might give (generic THROW, RETURN, QUIT, etc.)

        if (mode == LOOP_MAP_EACH)
            DS_DROP_TO(dsp_orig);

        return D_OUT;
    }

    // Note: This finalization will be run by finished loops as well as
    // interrupted ones.  So:
    //
    //    map-each x [1 2 3 4] [if x = 3 [break]] => [1 2]
    //
    //    map-each x [1 2 3 4] [if x = 3 [break/with "A"]] => [1 2 "A"]
    //
    //    every x [1 3 6 12] [if x = 6 [break/with 7] even? x] => 7
    //
    // This provides the most flexibility in the loop's processing, because
    // "override" logic already exists in the form of CATCH & THROW.

#if !defined(NDEBUG)
    if (LEGACY(OPTIONS_BREAK_WITH_OVERRIDES)) {
        // In legacy R3-ALPHA, BREAK without a provided value did *not*
        // override the result.  It returned the partial results.
        if (stop and NOT_END(D_OUT))
            return D_OUT;
    }
#endif

    switch (mode) {
    case LOOP_FOR_EACH:
        if (stop)
            return nullptr;
        return D_OUT;

    case LOOP_MAP_EACH:
        UNUSED(stop); // !!! MAP-EACH historically kept the remainder
        return Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
    }

    DEAD_END; // all branches handled in enum switch
}


//
//  for: native [
//
//  {Evaluate a block over a range of values. (See also: REPEAT)}
//
//      return: [<opt> any-value!]
//      'word [word!]
//          "Variable to hold current value"
//      start [any-series! any-number!]
//          "Starting value"
//      end [any-series! any-number!]
//          "Ending value"
//      bump [any-number!]
//          "Amount to skip each time"
//      body [block! action!]
//          "Code to evaluate"
//  ]
//
REBNATIVE(for)
{
    INCLUDE_PARAMS_OF_FOR;

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &context,
        ARG(word)
    );
    Init_Object(ARG(word), context); // keep GC safe

    REBVAL *var = CTX_VAR(context, 1); // not movable, see #2274

    if (
        IS_INTEGER(ARG(start))
        and IS_INTEGER(ARG(end))
        and IS_INTEGER(ARG(bump))
    ){
        return Loop_Integer_Common(
            D_OUT,
            var,
            ARG(body),
            VAL_INT64(ARG(start)),
            IS_DECIMAL(ARG(end))
                ? cast(REBI64, VAL_DECIMAL(ARG(end)))
                : VAL_INT64(ARG(end)),
            VAL_INT64(ARG(bump))
        );
    }

    if (ANY_SERIES(ARG(start))) {
        if (ANY_SERIES(ARG(end))) {
            return Loop_Series_Common(
                D_OUT,
                var,
                ARG(body),
                ARG(start),
                VAL_INDEX(ARG(end)),
                Int32(ARG(bump))
            );
        }
        else {
            return Loop_Series_Common(
                D_OUT,
                var,
                ARG(body),
                ARG(start),
                Int32s(ARG(end), 1) - 1,
                Int32(ARG(bump))
            );
        }
    }

    return Loop_Number_Common(
        D_OUT, var, ARG(body), ARG(start), ARG(end), ARG(bump)
    );
}


//
//  for-skip: native [
//
//  "Evaluates a block for periodic values in a series"
//
//      return: [<opt> any-value!]
//          {Last body result, or BREAK if null}
//      'word [word! blank!]
//          "Word that refers to the series, set to positions in the series"
//      skip [integer!]
//          "Number of positions to skip each time"
//      body [block! action!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_skip)
//
// !!! Should this fail on 0?  It could be that the loop will break for some
// other reason, and the author didn't wish to special case to rule out zero...
// generality may dictate allowing it.
{
    INCLUDE_PARAMS_OF_FOR_SKIP;

    REBVAL *word = ARG(word);

    if (IS_BLANK(word))
        return nullptr; // blank in, null out (same result as BREAK)

    Init_Void(D_OUT); // result if body never runs

    // Note that variable addresses may move on context expansion, protect
    // status can change, etc.  It must be re-fetched on each loop.
    //
    REBVAL *var = Get_Mutable_Var_May_Fail(word, SPECIFIED);
    if (IS_NULLED(var))
        fail (Error_No_Value(word));
    if (not ANY_SERIES(var))
        fail (Error_Invalid(var));

    // !!! We save the starting value and restore it on throws or when the
    // loop ends, but this restoration doesn't happen on FAILs.  Doing so
    // would require setting up a trap--should it?
    //
    DECLARE_LOCAL (saved);
    Move_Value(saved, var);
    PUSH_GC_GUARD(saved);

    REBINT skip = Int32(ARG(skip));

    // Starting location when past end with negative skip:
    //
    if (skip < 0 and VAL_INDEX(var) >= VAL_LEN_HEAD(var))
        VAL_INDEX(var) = VAL_LEN_HEAD(var) + skip;

    while (true) {
        REBINT len = VAL_LEN_HEAD(var); // VAL_LEN_HEAD() always >= 0
        REBINT index = VAL_INDEX(var); // (may have been set to < 0 below)

        if (index < 0)
            break;
        if (index >= len) {
            if (skip >= 0)
                break;
            index = len + skip; // negative
            if (index < 0)
                break;
            VAL_INDEX(var) = index;
        }

        if (Do_Branch_Throws(D_OUT, ARG(body))) {
            bool stop;
            if (not Catching_Break_Or_Continue(D_OUT, &stop)) {
                Move_Value(var, saved);
                DROP_GC_GUARD(saved);
                return D_OUT;
            }
            if (stop) {
                Move_Value(var, saved); // restore initial variable value
                DROP_GC_GUARD(saved);
                return nullptr;
            }
        }
        Voidify_If_Nulled(D_OUT); // null is reserved for BREAK

        // `var` must be refreshed each time arbitrary code runs, since the
        // context may expand and move the address, may get PROTECTed, etc.
        // Modifications to var are allowed, to another ANY-SERIES! value.
        //
        var = Get_Mutable_Var_May_Fail(word, SPECIFIED);
        if (IS_NULLED(var))
            fail (Error_No_Value(word));
        if (not ANY_SERIES(var))
            fail (Error_Invalid(var));

        VAL_INDEX(var) += skip;
    }

    Move_Value(var, saved); // restore initial variable value
    DROP_GC_GUARD(saved);
    return D_OUT;
}


//
//  forever: native [
//
//  "Evaluates a block endlessly, until an interrupting throw/error/break."
//
//      return: [<opt>]
//          {Void if BREAK}
//      body [block! action!]
//          "Block or action to evaluate each time"
//  ]
//
REBNATIVE(forever)
{
    INCLUDE_PARAMS_OF_FOREVER;

    do {
        if (Do_Branch_Throws(D_OUT, ARG(body))) {
            bool stop;
            if (not Catching_Break_Or_Continue(D_OUT, &stop))
                return D_OUT;
            if (stop)
                return nullptr;
        }
        // No need to voidify result, it doesn't escape...
    } while (true);

    DEAD_END;
}


//
//  for-each: native [
//
//  "Evaluates a block for each value(s) in a series."
//
//      return: [<opt> any-value!]
//          {Last body result, or null if BREAK}
//      'vars [word! lit-word! block!]
//          "Word or block of words to set each time, no new var if LIT-WORD!"
//      data [any-series! any-context! map! blank! datatype!]
//          "The series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(for_each)
{
    return Loop_Each(frame_, LOOP_FOR_EACH);
}


// For important reasons of semantics and performance, the REMOVE-EACH native
// does not actually perform removals "as it goes".  It could run afoul of
// any number of problems, including the mutable series becoming locked during
// the iteration.  Hence the iterated series is locked, and the removals are
// applied all at once atomically.
//
// However, this means that there's state which must be finalized on every
// possible exit path...be that BREAK, THROW, FAIL, or just ordinary finishing
// of the loop.  That finalization is done by this routine, which will clean
// up the state and remove any indicated items.  (It is assumed that all
// forms of exit, including raising an error, would like to apply any
// removals indicated thus far.)
//
// Because it's necessary to intercept, finalize, and then re-throw any
// fail() exceptions, rebRescue() must be used with a state structure.
//
struct Remove_Each_State {
    REBVAL *out;
    REBVAL *data;
    REBSER *series;
    const REBVAL *body;
    REBCTX *context;
    REBCNT start;
    REB_MOLD *mo;
};


// See notes on Remove_Each_State
//
static inline REBCNT Finalize_Remove_Each(struct Remove_Each_State *res)
{
    assert(GET_SER_INFO(res->series, SERIES_INFO_HOLD));
    CLEAR_SER_INFO(res->series, SERIES_INFO_HOLD);

    REBCNT count = 0;
    if (ANY_ARRAY(res->data)) {
        REBCNT len = VAL_LEN_HEAD(res->data);

        RELVAL *dest = VAL_ARRAY_AT(res->data);
        RELVAL *src = dest;

        // avoid blitting cells onto themselves by making the first thing we
        // do is to pass up all the unmarked (kept) cells.
        //
        while (NOT_END(src) and not (src->header.bits & NODE_FLAG_MARKED)) {
            ++src;
            ++dest;
        }

        // If we get here, we're either at the end, or all the cells from here
        // on are going to be moving to somewhere besides the original spot
        //
        for (; NOT_END(dest); ++dest, ++src) {
            while (NOT_END(src) and (src->header.bits & NODE_FLAG_MARKED)) {
                ++src;
                --len;
                ++count;
            }
            if (IS_END(src)) {
                TERM_ARRAY_LEN(VAL_ARRAY(res->data), len);
                return count;
            }
            Blit_Cell(dest, src); // same array--rare place we can do this
        }

        // If we get here, there were no removals, and length is unchanged.
        //
        assert(count == 0);
        assert(len == VAL_LEN_HEAD(res->data));
    }
    else if (IS_BINARY(res->data)) {
        //
        // If there was a BREAK, THROW, or fail() we need the remaining data
        //
        REBCNT orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);
        Append_Unencoded_Len(
            res->mo->series,
            cs_cast(BIN_AT(res->series, res->start)),
            orig_len - res->start
        );

        // !!! We are reusing the mold buffer, but *not putting UTF-8 data*
        // into it.  Revisit if this inhibits cool UTF-8 based tricks the
        // mold buffer might do otherwise.
        //
        REBSER *popped = Pop_Molded_Binary(res->mo);

        assert(SER_LEN(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - SER_LEN(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Series_Content(popped, VAL_SERIES(res->data));

        Free_Unmanaged_Series(popped); // now frees incoming series's data
    }
    else {
        assert(ANY_STRING(res->data));

        // If there was a BREAK, THROW, or fail() we need the remaining data
        //
        REBCNT orig_len = VAL_LEN_HEAD(res->data);
        assert(res->start <= orig_len);

        for (; res->start != orig_len; ++res->start) {
            Append_Utf8_Codepoint(
                res->mo->series,
                GET_ANY_CHAR(res->series, res->start)
            );
        }

        REBSER *popped = Pop_Molded_String(res->mo);

        assert(SER_LEN(popped) <= VAL_LEN_HEAD(res->data));
        count = VAL_LEN_HEAD(res->data) - SER_LEN(popped);

        // We want to swap out the data properties of the series, so the
        // identity of the incoming series is kept but now with different
        // underlying data.
        //
        Swap_Series_Content(popped, VAL_SERIES(res->data));

        Free_Unmanaged_Series(popped); // now frees incoming series's data
    }

    return count;
}


// See notes on Remove_Each_State
//
static REBVAL *Remove_Each_Core(struct Remove_Each_State *res)
{
    // Set a bit saying we are iterating the series, which will disallow
    // mutations (including a nested REMOVE-EACH) until completion or failure.
    // This flag will be cleaned up by Finalize_Remove_Each(), which is run
    // even if there is a fail().
    //
    SET_SER_INFO(res->series, SERIES_INFO_HOLD);

    bool stop = false;
    REBCNT index = res->start; // declare here, avoid longjmp clobber warnings

    REBCNT len = SER_LEN(res->series); // temp read-only, this won't change
    while (index < len and not stop) {
        assert(res->start == index);

        REBVAL *var = CTX_VAR(res->context, 1); // not movable, see #2274
        for (; NOT_END(var); ++var) {
            if (index == len) {
                //
                // The second iteration here needs x = #"c" and y as void.
                //
                //     data: copy "abc"
                //     remove-each [x y] data [...]
                //
                Init_Nulled(var);
                continue; // the `for` loop setting variables
            }

            if (ANY_ARRAY(res->data))
                Derelativize(
                    var,
                    VAL_ARRAY_AT_HEAD(res->data, index),
                    VAL_SPECIFIER(res->data)
                );
            else if (IS_BINARY(res->data))
                Init_Integer(var, cast(REBI64, BIN_HEAD(res->series)[index]));
            else {
                assert(ANY_STRING(res->data));
                Init_Char(var, GET_ANY_CHAR(res->series, index));
            }
            ++index;
        }

        if (Do_Branch_Throws(res->out, res->body)) {
            if (not Catching_Break_Or_Continue(res->out, &stop)) {
                assert(THROWN(res->out)); // how caller knows it threw
                return NULL; // we'll bubble it up, but will also finalize
            }

            if (stop) {
                //
                // BREAK - res->out may not be void if /WITH refinement used
            }
            else {
                // CONTINUE - res->out may not be void if /WITH refinement used
            }
        }
        if (IS_VOID(res->out))
            fail (Error_Void_Conditional_Raw()); // neither true nor false

        if (ANY_ARRAY(res->data)) {
            if (IS_NULLED(res->out) or IS_FALSEY(res->out)) {
                res->start = index;
                continue; // keep requested, don't mark for culling
            }

            do {
                assert(res->start <= len);
                VAL_ARRAY_AT_HEAD(res->data, res->start)->header.bits
                    |= NODE_FLAG_MARKED;
                ++res->start;
            } while (res->start != index);
        }
        else {
            if (not IS_NULLED(res->out) and IS_TRUTHY(res->out)) {
                res->start = index;
                continue; // remove requested, don't save to buffer
            }

            do {
                assert(res->start <= len);
                if (IS_BINARY(res->data)) {
                    Append_Unencoded_Len(
                        res->mo->series,
                        cs_cast(BIN_AT(res->series, res->start)),
                        1
                    );
                }
                else {
                    Append_Utf8_Codepoint(
                        res->mo->series, GET_ANY_CHAR(res->series, res->start)
                    );
                }
                ++res->start;
            } while (res->start != index);
        }
    }

    // We get here on normal completion or a BREAK
    // THROW will return above

    // Finalize may need to process residual data in the case of BREAK
    // It knows this based on res.start < len
    //
    assert((stop and res->start <= len) or (not stop and res->start == len));

    if (stop) {
        //
        // !!! Should the return conventions of REMOVE-EACH honor the
        // "loop protocol" where a broken loop returns BLANK!?
    }

    return NULL;
}


//
//  remove-each: native [
//
//  {Removes values for each block that returns true.}
//
//      return: [integer!]
//          {Number of removed series items}
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series!]
//          "The series to traverse (modified)" ; should BLANK! opt-out?
//      body [block!]
//          "Block to evaluate (return TRUE to remove)"
//  ]
//
REBNATIVE(remove_each)
{
    INCLUDE_PARAMS_OF_REMOVE_EACH;

    struct Remove_Each_State res;
    res.data = ARG(data);

    // !!! Currently there is no support for VECTOR!, or IMAGE! (what would
    // that even *mean*?) yet these are in the ANY-SERIES! typeset.
    //
    if (not (
        ANY_ARRAY(res.data) or ANY_STRING(res.data) or IS_BINARY(res.data)
    )){
        fail (Error_Invalid(res.data));
    }

    // Check the series for whether it is read only, in which case we should
    // not be running a REMOVE-EACH on it.  This check for permissions applies
    // even if the REMOVE-EACH turns out to be a no-op.
    //
    res.series = VAL_SERIES(res.data);
    FAIL_IF_READ_ONLY_SERIES(res.series);

    if (VAL_INDEX(res.data) >= SER_LEN(res.series)) {
        //
        // If index is past the series end, then there's nothing removable.
        //
        // !!! Should REMOVE-EACH follow the "loop conventions" where if the
        // body never gets a chance to run, the return value is void?
        //
        return Init_Integer(D_OUT, 0);
    }

    // Create a context for the loop variables, and bind the body to it.
    // Do this before PUSH_TRAP, so that if there is any failure related to
    // memory or a poorly formed ARG(vars) that it doesn't try to finalize
    // the REMOVE-EACH, as `res` is not ready yet.
    //
    Virtual_Bind_Deep_To_New_Context(
        ARG(body), // may be updated, will still be GC safe
        &res.context,
        ARG(vars)
    );
    Init_Object(ARG(vars), res.context); // keep GC safe
    res.body = ARG(body);

    res.start = VAL_INDEX(res.data);

    REB_MOLD mold_struct;
    if (ANY_ARRAY(res.data)) {
        //
        // We're going to use NODE_FLAG_MARKED on the elements of data's
        // array for those items we wish to remove later.
        //
        // !!! This may not be better than pushing kept values to the data
        // stack and then creating a precisely-sized output blob to swap as
        // the underlying memory for the array.  (Imagine a large array from
        // which there are many removals, and the ensuing wasted space being
        // left behind).  But worth testing the technique of marking in case
        // it's ever required for other scenarios.
        //
        TRASH_POINTER_IF_DEBUG(res.mo);
    }
    else {
        // We're going to generate a new data allocation, but then swap its
        // underlying content to back the series we were given.  (See notes
        // above on how this might be the better way to deal with arrays too.)
        //
        // !!! Uses the mold buffer even for binaries, and since we know
        // we're never going to be pushing a value bigger than 0xFF it will
        // not require a wide string.  So the series we pull off should be
        // byte-sized.  In a sense this is wasteful and there should be a
        // byte-buffer-backed parallel to mold, but the logic for nesting mold
        // stacks already exists and the mold buffer is "hot", so it's not
        // necessarily *that* wasteful in the scheme of things.
        //
        CLEARS(&mold_struct);
        res.mo = &mold_struct;
        Push_Mold(res.mo);
    }

    SET_END(D_OUT); // will be tested for THROWN() to signal a throw happened
    res.out = D_OUT;

    REBVAL *error = rebRescue(cast(REBDNG*, &Remove_Each_Core), &res);

    // Currently, if a fail() happens during the iteration, any removals
    // which were indicated will be enacted before propagating failure.
    //
    REBCNT removals = Finalize_Remove_Each(&res);

    if (error)
        rebJumps("FAIL", error, rebEND);

    if (THROWN(res.out))
        return D_OUT;

    return Init_Integer(D_OUT, removals);
}


//
//  map-each: native [
//
//  {Evaluate a block for each value(s) in a series and collect as a block.}
//
//      return: [<opt> block!]
//          {Collected block (BREAK/WITH can add a final result to block)}
//      'vars [word! block!]
//          "Word or block of words to set each time (local)"
//      data [any-series! blank!]
//          "The series to traverse, blank to opt out"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(map_each)
{
    return Loop_Each(frame_, LOOP_MAP_EACH);
}


//
//  loop: native [
//
//  "Evaluates a block a specified number of times."
//
//      return: [<opt> any-value!]
//          {Last body result, or null if BREAK}
//      count [any-number! logic! blank!]
//          "Repetitions (true loops infinitely, false doesn't run)"
//      body [block! action!]
//          "Block to evaluate or action to run."
//  ]
//
REBNATIVE(loop)
{
    INCLUDE_PARAMS_OF_LOOP;

    if (IS_BLANK(ARG(count)))
        return nullptr; // BLANK in, NULL out (same output as BREAK)

    if (IS_FALSEY(ARG(count))) {
        assert(IS_LOGIC(ARG(count))); // is false...opposite of infinite loop
        return Init_Void(D_OUT);
    }

    Init_Void(D_OUT); // result if body never runs

    REBI64 count;

    if (IS_LOGIC(ARG(count))) {
        assert(VAL_LOGIC(ARG(count)) == true);

        // Run forever, and as a micro-optimization don't handle specially
        // in the loop, just seed with a very large integer.  In the off
        // chance that is exhaust it, jump here to re-seed and loop again.
    restart:
        count = INT64_MAX;
    }
    else
        count = Int64(ARG(count));

    for (; count > 0; count--) {
        if (Do_Branch_Throws(D_OUT, ARG(body))) {
            bool stop;
            if (not Catching_Break_Or_Continue(D_OUT, &stop))
                return D_OUT;
            if (stop)
                return nullptr;
        }
        Voidify_If_Nulled(D_OUT); // null is reserved for BREAK
    }

    if (IS_LOGIC(ARG(count)))
        goto restart; // "infinite" loop exhausted MAX_I64 steps (rare case)

    return D_OUT;
}


//
//  repeat: native [
//
//  {Evaluates a block a number of times or over a series.}
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value}
//      'word [word!]
//          "Word to set each time"
//      value [any-number! any-series! blank!]
//          "Maximum number or series to traverse"
//      body [block!]
//          "Block to evaluate each time"
//  ]
//
REBNATIVE(repeat)
{
    INCLUDE_PARAMS_OF_REPEAT;

    REBVAL *value = ARG(value);

    if (IS_BLANK(value))
        return nullptr; // blank in, void out (same result as BREAK)

    if (IS_DECIMAL(value) or IS_PERCENT(value))
        Init_Integer(value, Int64(value));

    REBCTX *context;
    Virtual_Bind_Deep_To_New_Context(
        ARG(body),
        &context,
        ARG(word)
    );
    Init_Object(ARG(word), context); // keep GC safe

    assert(CTX_LEN(context) == 1);

    REBVAL *var = CTX_VAR(context, 1); // not movable, see #2274
    if (ANY_SERIES(value))
        return Loop_Series_Common(
            D_OUT, var, ARG(body), value, VAL_LEN_HEAD(value) - 1, 1
        );

    REBI64 n = VAL_INT64(value);
    if (n < 1) // Loop_Integer from 1 to 0 with bump of 1 is infinite
        return Init_Void(D_OUT); // void if loop condition never runs

    return Loop_Integer_Common(
        D_OUT, var, ARG(body), 1, VAL_INT64(value), 1
    );
}


// Common code for UNTIL & UNTIL-NOT (same frame param layout)
//
inline static void Until_Core(
    REBVAL *out,
    const REBVAL *body,
    bool trigger // body keeps running so long as evaluation matches this
){
    do {

    skip_check:;

        if (Do_Branch_Throws(out, body)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return;
            if (stop) {
                Init_Nulled(out);
                return;
            }

            // UNTIL and UNTIL-NOT both follow the precedent that the way
            // a CONTINUE/WITH works is to act as if the loop body returned
            // the value passed to the WITH.  Since the condition and body are
            // the same in this case, the implications are a strange, though
            // logical.  CONTINUE/WITH FALSE will break UNTIL-NOT, and
            // CONTINUE/WITH TRUE breaks UNTIL.
            //
            // But this is different for null, since loop bodies returning
            // conditions must be true or false...and continue needs to work.
            // Hence it just means to continue either way.
            //
            if (IS_NULLED(out))
                goto skip_check;
        }
        else { // didn't throw, see above about null difference from CONTINUE
            if (IS_VOID(out))
                fail (Error_Void_Conditional_Raw());
        }
    } while (IS_TRUTHY(out) == trigger);
}


//
//  until: native [
//
//  "Evaluates the body until it evaluates to a conditionally true value"
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value.}
//      body [block! action!]
//  ]
//
REBNATIVE(until)
//
// Note: There were wide-ranging debates on whether UNTIL should be arity-2 as
// a parallel to WHILE.  In light of all the tradeoffs, it is kept this way.
{
    INCLUDE_PARAMS_OF_UNTIL;

    Until_Core(
        D_OUT,
        ARG(body),
        false // loop body runs so long as it IS_FALSEY()
    );
    return D_OUT;
}


//
//  until-not: native [
//
//  "Evaluates the body until it evaluates to a conditionally false value"
//
//      return: [<opt> any-value!]
//          {Last body result or BREAK value.}
//      body [block! action!]
//  ]
//
REBNATIVE(until_not)
//
// Faster than running NOT, and doesn't need groups for `until [...not (x =`
{
    INCLUDE_PARAMS_OF_UNTIL_NOT;

    Until_Core(
        D_OUT,
        ARG(body),
        true // loop body runs so long as it IS_TRUTHY()
    );
    return D_OUT;
}


// Common code for WHILE & WHILE-NOT
//
inline static void While_Core(
    REBVAL *out,
    const REBVAL *condition,
    const REBVAL *body,
    bool trigger, // body keeps running so long as condition matches
    REBVAL *cell // GC-safe temporary cell
){
    Init_Void(out); // result if body never runs

    do {
        if (Do_Branch_Throws(cell, condition)) {
            Move_Value(out, cell);
            return; // don't see BREAK/CONTINUE in the *condition*
        }

        if (IS_VOID(cell))
            fail (Error_Void_Conditional_Raw()); // neither truthy nor falsey

        if (IS_TRUTHY(cell) != trigger)
            return; // loop trigger didn't match, return last body result

        if (Do_Branch_With_Throws(out, body, cell)) {
            bool stop;
            if (not Catching_Break_Or_Continue(out, &stop))
                return;
            if (stop) {
                Init_Nulled(out);
                return;
            }
        }
        Voidify_If_Nulled(out); // NULL is reserved for BREAK
    } while (true);

    DEAD_END;
}


//
//  while: native [
//
//  {While a condition is conditionally true, evaluates the body.}
//
//      return: [<opt> any-value!]
//          "Last body result, or null if BREAK"
//      condition [block! action!]
//      body [block! action!]
//  ]
//
REBNATIVE(while)
{
    INCLUDE_PARAMS_OF_WHILE;

    DECLARE_LOCAL (cell);
    SET_END(cell);
    PUSH_GC_GUARD(cell);
    While_Core(
        D_OUT,
        ARG(condition),
        ARG(body),
        true, // loop body runs so long as condition IS_TRUTHY()
        cell
    );
    DROP_GC_GUARD(cell);
    return D_OUT;
}


//
//  while-not: native [
//
//  {While a condition is conditionally false, evaluate the body.}
//
//      return: [<opt> any-value!]
//          "Last body result, or null if BREAK"
//      condition [block! action!]
//      body [block! action!]
//  ]
//
REBNATIVE(while_not)
//
// Faster than running NOT, and doesn't need groups for `while [not (x =`
{
    INCLUDE_PARAMS_OF_WHILE_NOT;

    DECLARE_LOCAL (cell);
    SET_END(cell);
    PUSH_GC_GUARD(cell);
    While_Core(
        D_OUT,
        ARG(condition),
        ARG(body),
        false, // loop body runs so long as trigger IS_FALSEY()
        cell
    );
    DROP_GC_GUARD(cell);
    return D_OUT;
}
