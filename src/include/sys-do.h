//
//  File: %sys-do.h
//  Summary: {DO-until-end (of block or variadic feed) evaluation API}
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
// DO is a higher-level concept, built on top of EVALUATE.  It always implies
// running to the end of its input, and always produces a single value...
// typically the last value an evaluation step computed.
//
// If no evaluative product can be produced (as in `do [comment "hi"]` or
// `do [| | ()]` or just plain `do []`) then Do_XXX() will synthesize a VOID!.
//


inline static bool Do_At_Mutable_Throws(  // no way to pass in FEED_FLAG_CONST
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier
){
    return Eval_Array_At_Mutable_Throws_Core(
        Init_Void(out),
        nullptr,  // opt_first (null indicates nothing, not nulled cell)
        array,
        index,
        specifier,
        EVAL_MASK_DEFAULT
    );
}


inline static REBIXO Eval_Step_In_Any_Array_At_Core(
    REBVAL *out,
    const RELVAL *any_array,  // Note: legal to have any_array = out
    REBSPC *specifier,
    REBFLGS flags
){
    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    if (IS_END(feed->value))
        return END_FLAG;

    DECLARE_FRAME (f, feed, flags);

    Push_Frame(out, f);
    bool threw = (*PG_Eval_Throws)(f);
    Drop_Frame(f);

    if (threw)
        return THROWN_FLAG;

    if (f->feed->index == VAL_LEN_HEAD(any_array) + 1)
        return END_FLAG;

    return f->feed->index;
}

inline static bool Eval_Any_Array_At_Throws_Core(
    REBVAL *out,
    const RELVAL *any_array,  // Note: legal to have any_array = out
    REBSPC *specifier,
    REBFLGS flags
){
    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    if (IS_END(feed->value))
        return false;

    DECLARE_FRAME (f, feed, flags);

    bool threw;
    Push_Frame(out, f);
    do {
        threw = (*PG_Eval_Throws)(f);
    } while (not threw and NOT_END(feed->value));
    Drop_Frame(f);

    return threw;
}

inline static bool Do_Any_Array_At_Core_Throws(
    REBVAL *out,
    const RELVAL *any_array,
    REBSPC *specifier
){
    assert(out != any_array);  // the Init_Void() would corrupt it
    return Eval_Any_Array_At_Throws_Core(
        Init_Void(out),
        any_array,
        specifier,
        EVAL_MASK_DEFAULT
    );
}

inline static bool Do_Any_Array_At_Throws(
    REBVAL *out,
    const REBVAL *any_array
){
    return Do_Any_Array_At_Core_Throws(
        out,
        any_array,
        VAL_SPECIFIER(any_array)
    );
}


inline static bool Do_Va_Throws(
    REBVAL *out,
    const void *opt_first,
    va_list *vaptr  // va_end() handled by Eval_Va_Core on success/fail/throw
){
    return Eval_Va_Throws_Core(
        Init_Void(out),
        opt_first,
        vaptr,
        EVAL_MASK_DEFAULT
    );
}


// Takes a list of arguments terminated by an end marker and will do something
// similar to R3-Alpha's "apply/only" with a value.  If that value is a
// function, it will be called...if it's a SET-WORD! it will be assigned, etc.
//
// This is equivalent to putting the value at the head of the input and
// then calling EVAL/ONLY on it.  If all the inputs are not consumed, an
// error will be thrown.
//
inline static bool Run_Throws(
    REBVAL *out,
    bool fully,
    const void *p,  // last param before ... mentioned in va_start()
    ...
){
    va_list va;
    va_start(va, p);

    bool threw = Eval_Step_In_Va_Throws_Core(
        SET_END(out),  // start at END to detect error if no eval product
        p,  // opt_first
        &va,  // va_end() handled by Eval_Va_Core on success/fail/throw
        EVAL_MASK_DEFAULT
            | (fully ? EVAL_FLAG_NO_RESIDUE : 0)
    );

    if (IS_END(out))
        fail ("Run_Throws() empty or just COMMENTs/ELIDEs/BAR!s");

    return threw;
}


// Conditional constructs allow branches that are either BLOCK!s or ACTION!s.
// If an action, the triggering condition is passed to it as an argument:
// https://trello.com/c/ay9rnjIe
//
// Allowing other values was deemed to do more harm than good:
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
inline static bool Do_Branch_Core_Throws(
    REBVAL *out,
    const REBVAL *branch,
    const REBVAL *condition // can be END or nullptr--can't be a NULLED cell!
){
    assert(branch != out and condition != out);

    if (IS_QUOTED(branch)) {
        Unquotify(Move_Value(out, branch), 1);
        return false;
    }

    if (IS_BLOCK(branch))
        return Do_Any_Array_At_Throws(out, branch);

    assert(IS_ACTION(branch));
    return Run_Throws(
        out,
        false, // !fully, e.g. arity-0 functions can ignore condition
        rebEVAL,
        branch,
        condition, // may be an END marker, if not Do_Branch_With() case
        rebEND // ...but if condition wasn't an END marker, we need one
    );
}

#define Do_Branch_With_Throws(out,branch,condition) \
    Do_Branch_Core_Throws((out), (branch), NULLIFY_NULLED(condition))

#define Do_Branch_Throws(out,branch) \
    Do_Branch_Core_Throws((out), (branch), END_NODE)
