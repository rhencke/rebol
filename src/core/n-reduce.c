//
//  File: %n-reduce.h
//  Summary: {REDUCE and COMPOSE natives and associated service routines}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

#include "sys-core.h"

//
//  Reduce_Any_Array_Throws: C
//
// Reduce array from the index position specified in the value.
//
// If `into` then splice into the existing `out`.  Otherwise, overwrite the
// `out` with all values collected from the stack, into an array matching the
// type of the input.  So [1 + 1 2 + 2] => [3 4], and 1/+/1/2/+/2 => 3/4
//
// !!! This is not necessarily the best answer, it's just the mechanically
// most obvious one.
//
REBOOL Reduce_Any_Array_Throws(
    REBVAL *out,
    REBVAL *any_array,
    REBFLGS flags
){
    // Can't have more than one policy on null conversion in effect.
    //
    assert(not ((flags & REDUCE_FLAG_TRY) and (flags & REDUCE_FLAG_OPT)));

    REBDSP dsp_orig = DSP;

    DECLARE_FRAME (f);
    Push_Frame(f, any_array);

    DECLARE_LOCAL (reduced);

    while (FRM_HAS_MORE(f)) {
        REBOOL line = GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE);

        if (Do_Next_In_Frame_Throws(reduced, f)) {
            Move_Value(out, reduced);
            DS_DROP_TO(dsp_orig);
            Abort_Frame(f);
            return TRUE;
        }

        if (IS_VOID(reduced)) {
            if (flags & REDUCE_FLAG_TRY) {
                DS_PUSH_TRASH;
                Init_Blank(DS_TOP);
                if (line)
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }
            else if (not (flags & REDUCE_FLAG_OPT))
                fail (Error_Reduce_Made_Null_Raw());
        }
        else {
            DS_PUSH(reduced);
            if (line)
                SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
        }
    }

    if (flags & REDUCE_FLAG_INTO)
        Pop_Stack_Values_Into(out, dsp_orig);
    else {
        REBFLGS pop_flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
        if (GET_SER_FLAG(VAL_ARRAY(any_array), ARRAY_FLAG_TAIL_NEWLINE))
            pop_flags |= ARRAY_FLAG_TAIL_NEWLINE;

        Init_Any_Array(
            out,
            VAL_TYPE(any_array),
            Pop_Stack_Values_Core(dsp_orig, pop_flags)
        );
    }

    Drop_Frame(f);
    return FALSE;
}


//
//  reduce: native [
//
//  {Evaluates expressions, keeping each result (DO only gives last result)}
//
//      return: "New array or value"
//          [<opt> any-value!]
//      value "GROUP! and BLOCK! evaluate each item, single values evaluate"
//          [any-value!]
//      /into "Output results into a series with no intermediate storage"
//      target [any-array!]
//      /try "If an evaluation returns null, convert to blank vs. failing"
//      /opt "If an evaluation returns null, omit the result" ; !!! EXPERIMENT
//  ]
//
REBNATIVE(reduce)
{
    INCLUDE_PARAMS_OF_REDUCE;

    REBVAL *value = ARG(value);

    if (REF(opt) and REF(try))
        fail (Error_Bad_Refines_Raw());

    if (IS_BLOCK(value) or IS_GROUP(value)) {
        if (REF(into))
            Move_Value(D_OUT, ARG(target));

        if (Reduce_Any_Array_Throws(
            D_OUT,
            value,
            REDUCE_MASK_NONE
                | (REF(into) ? REDUCE_FLAG_INTO : 0)
                | (REF(try) ? REDUCE_FLAG_TRY : 0)
                | (REF(opt) ? REDUCE_FLAG_OPT : 0)
        )){
            return R_OUT_IS_THROWN;
        }

        return R_OUT;
    }

    // Single element REDUCE does an EVAL, but doesn't allow arguments.
    // (R3-Alpha, would just return the input, e.g. `reduce :foo` => :foo)
    // If there are arguments required, Eval_Value_Throws() will error.
    //
    // !!! Should the error be more "reduce-specific" if args were required?
    //
    if (ANY_INERT(value)) {
        Move_Value(D_OUT, value);
    }
    else if (Eval_Value_Throws(D_OUT, value))
        return R_OUT_IS_THROWN;

    if (not REF(into)) { // just return the evaluated item if no /INTO target
        if (IS_VOID(D_OUT)) {
            if (REF(try))
                return R_BLANK;

            // Don't bother erroring if not REF(opt).  Since we *can* return a
            // void result for a non-BLOCK!/GROUP!, the caller will have to
            // worry about whether to error on that themselves.
            //
            return R_NULL;
        }
        return R_OUT;
    }

    REBVAL *into = ARG(target);
    assert(ANY_ARRAY(into));
    FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(into)); // should fail even if no-op

    if (IS_VOID(D_OUT)) { // null insertions are no-op if /OPT, else fail
        if (not REF(opt))
            fail ("null cannot be inserted /INTO target...use REDUCE/OPT");

        Move_Value(D_OUT, into);
        return R_OUT;
    }


    // Insert the single item into the target array at its current position,
    // and return the position after the insertion (the /INTO convention)

    REBCNT after = Insert_Series(
        SER(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, D_OUT),
        1 // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );
    Move_Value(D_OUT, into);
    VAL_INDEX(D_OUT) = after;
    return R_OUT;
}


// R3-Alpha only COMPOSE'd GROUP!s.  This allows for more flexible choices,
// by giving delimiter patterns for substitutions.
//
static inline const RELVAL *Match_For_Compose(
    REBSPC **specifier_out,
    const RELVAL *value,
    const RELVAL *pattern,
    REBSPC *specifier
){
    assert(IS_GROUP(pattern) || IS_BLOCK(pattern));

    if (VAL_TYPE(value) != VAL_TYPE(pattern))
        return NULL;

    RELVAL *p = VAL_ARRAY_AT(pattern);
    if (IS_END(p)) {
        *specifier_out = Derive_Specifier(specifier, value);
        return value; // e.g. () matching (a b c)
    }

    RELVAL *v = VAL_ARRAY_AT(value);
    if (IS_END(v))
        return NULL; // e.g. (()) can't match ()

    if (not ANY_ARRAY(p) or NOT_END(p + 1)) {
        //
        // !!! Today's patterns are a bit limited, since there is no DO/PART
        // the situation is: `[** you can't stop at a terminal sigil -> **]`
        //
        fail ("Bad CONCOCT Pattern, currently must be like (([()]))");
    }

    if (not ANY_ARRAY(v) or NOT_END(v + 1))
        return NULL; // e.g. (()) can't match (() a b c)

    // Due to the nature of the matching, cycles in this recursion *shouldn't*
    // matter...if both the pattern and the value are cyclic, they'll still
    // either match or not.
    //
    return Match_For_Compose(
        specifier_out,
        v,
        p,
        Derive_Specifier(specifier, v)
    );
}


//
//  Compose_Any_Array_Throws: C
//
// Compose a block from a block of un-evaluated values and GROUP! arrays that
// are evaluated.  This calls into Do_Core, so if 'into' is provided, then its
// series must be protected from garbage collection.
//
//     deep - recurse into sub-blocks
//     only - parens that return blocks are kept as blocks
//
// Writes result value at address pointed to by out.
//
REBOOL Compose_Any_Array_Throws(
    REBVAL *out,
    const REBVAL *any_array,
    const REBVAL *pattern,
    REBOOL deep,
    REBOOL only,
    REBOOL into
) {
    REBDSP dsp_orig = DSP;

    DECLARE_FRAME (f);
    Push_Frame(f, any_array);

    DECLARE_LOCAL (composed);
    DECLARE_LOCAL (specific);

    while (FRM_HAS_MORE(f)) {
        REBOOL line = GET_VAL_FLAG(f->value, VALUE_FLAG_NEWLINE_BEFORE);

        REBSPC *match_specifier;
        const RELVAL *match = Match_For_Compose(
            &match_specifier,
            f->value,
            pattern,
            f->specifier
        );

        if (match) {
            //
            // Evaluate the GROUP! at current position into `composed` cell.
            //
            if (Do_At_Throws(
                composed,
                VAL_ARRAY(match),
                VAL_INDEX(match),
                match_specifier
            )){
                Move_Value(out, composed);
                DS_DROP_TO(dsp_orig);
                Abort_Frame(f);
                return TRUE;
            }

            Fetch_Next_In_Frame(f);

            if (IS_BLOCK(composed) and not only) {
                //
                // compose [blocks ([a b c]) merge] => [blocks a b c merge]
                //
                RELVAL *push = VAL_ARRAY_AT(composed);
                while (NOT_END(push)) {
                    //
                    // `evaluated` is known to be specific, but its specifier
                    // may be needed to derelativize its children.
                    //
                    DS_PUSH_RELVAL(push, VAL_SPECIFIER(composed));
                    if (line) {
                        SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
                        line = FALSE;
                    }
                    push++;
                }
            }
            else if (not IS_VOID(composed)) {
                //
                // compose [(1 + 2) inserts as-is] => [3 inserts as-is]
                // compose/only [([a b c]) unmerged] => [[a b c] unmerged]
                //
                DS_PUSH(composed);
                if (line)
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }
            else {
                //
                // compose [(print "Voids *vanish*!")] => []
                //
            }
        }
        else if (deep) {
            //
            // Historically, ANY-PATH! was not seen as a candidate for /DEEP
            // traversal.  GROUP! was not a possibility (as it was always
            // composed).  With generalized CONCOCT, it is possible for those
            // who wish to leave GROUP! in PATH! untouched to do so--and more
            // obvious to treat all ANY-ARRAY! types equal.
            //
            if (ANY_ARRAY(f->value)) {
                //
                // compose/deep [does [(1 + 2)] nested] => [does [3] nested]

                Derelativize(specific, f->value, f->specifier);

                if (Compose_Any_Array_Throws(
                    composed,
                    specific,
                    pattern,
                    TRUE,
                    only,
                    into
                )) {
                    Move_Value(out, composed);
                    DS_DROP_TO(dsp_orig);
                    Abort_Frame(f);
                    return TRUE;
                }

                DS_PUSH(composed);
                if (line)
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }
            else {
                if (ANY_ARRAY(f->value)) {
                    //
                    // compose [copy/(orig) (copy)] => [copy/(orig) (copy)]
                    // !!! path and second group are copies, first group isn't
                    //
                    REBSPC *derived = Derive_Specifier(f->specifier, f->value);
                    REBARR *copy = Copy_Array_Shallow(
                        VAL_ARRAY(f->value),
                        derived
                    );
                    DS_PUSH_TRASH;
                    Init_Any_Array_At(
                        DS_TOP, VAL_TYPE(f->value), copy, VAL_INDEX(f->value)
                    ); // ...manages
                }
                else
                    DS_PUSH_RELVAL(f->value, f->specifier);

                if (line)
                    SET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE);
            }
            Fetch_Next_In_Frame(f);
        }
        else {
            //
            // compose [[(1 + 2)] (reverse "wollahs")] => [[(1 + 2)] "shallow"]
            //
            DS_PUSH_RELVAL(f->value, f->specifier);
            assert(line == GET_VAL_FLAG(DS_TOP, VALUE_FLAG_NEWLINE_BEFORE));
            Fetch_Next_In_Frame(f);
        }
    }

    if (into)
        Pop_Stack_Values_Into(out, dsp_orig);
    else {
        REBFLGS flags = NODE_FLAG_MANAGED | ARRAY_FLAG_FILE_LINE;
        if (GET_SER_FLAG(VAL_ARRAY(any_array), ARRAY_FLAG_TAIL_NEWLINE))
            flags |= ARRAY_FLAG_TAIL_NEWLINE;

        Init_Any_Array(
            out,
            VAL_TYPE(any_array),
            Pop_Stack_Values_Core(dsp_orig, flags)
        );
    }

    Drop_Frame(f);
    return FALSE;
}


//
//  concoct: native [
//
//  {Evaluates only contents of pattern-delimited expressions in an array.}
//
//      return: [any-array!]
//      :pattern [group! block!]
//          "Pattern like (([()])), to recognize and do evaluations for"
//      value [any-array!]
//          "Array to compose"
//      /deep
//          "Compose nested BLOCK!s and GROUP!s (ANY-PATH! not considered)"
//      /only
//          {Insert BLOCK!s as a single value (not the contents of the block)}
//      /into
//          {Output results into a series with no intermediate storage}
//      out [any-array! any-string! binary!]
//  ]
//
REBNATIVE(concoct)
//
// Note: COMPOSE is a specialization of CONCOCT where the pattern is ()
{
    INCLUDE_PARAMS_OF_CONCOCT;

    // Compose_Values_Throws() expects `out` to contain the target if it is
    // passed TRUE as the `into` flag.
    //
    if (REF(into))
        Move_Value(D_OUT, ARG(out));
    else
        assert(IS_END(D_OUT)); // !!! guaranteed, better signal than `into`?

    if (Compose_Any_Array_Throws(
        D_OUT,
        ARG(value),
        ARG(pattern),
        REF(deep),
        REF(only),
        REF(into)
    )) {
        return R_OUT_IS_THROWN;
    }

    return R_OUT;
}


enum FLATTEN_LEVEL {
    FLATTEN_NOT,
    FLATTEN_ONCE,
    FLATTEN_DEEP
};


static void Flatten_Core(
    RELVAL head[],
    REBSPC *specifier,
    enum FLATTEN_LEVEL level
) {
    RELVAL *item = head;
    for (; NOT_END(item); ++item) {
        if (IS_BLOCK(item) and level != FLATTEN_NOT) {
            REBSPC *derived = Derive_Specifier(specifier, item);
            Flatten_Core(
                VAL_ARRAY_AT(item),
                derived,
                level == FLATTEN_ONCE ? FLATTEN_NOT : FLATTEN_DEEP
            );
        }
        else
            DS_PUSH_RELVAL(item, specifier);
    }
}


//
//  flatten: native [
//
//  {Flattens a block of blocks.}
//
//      return: [block!]
//          {The flattened result block}
//      block [block!]
//          {The nested source block}
//      /deep
//  ]
//
REBNATIVE(flatten)
{
    INCLUDE_PARAMS_OF_FLATTEN;

    REBDSP dsp_orig = DSP;

    Flatten_Core(
        VAL_ARRAY_AT(ARG(block)),
        VAL_SPECIFIER(ARG(block)),
        REF(deep) ? FLATTEN_DEEP : FLATTEN_ONCE
    );

    Init_Block(D_OUT, Pop_Stack_Values(dsp_orig));
    return R_OUT;
}
