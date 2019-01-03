//
//  File: %t-literal.c
//  Summary: "LITERAL! datatype that acts as container for ANY-VALUE!"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Rebol Open Source Contributors
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
// In historical Rebol, a WORD! and PATH! had variants which were "LIT" types.
// e.g. FOO was a word, while 'FOO was a LIT-WORD!.  The evaluator behavior
// was that the literalness would be removed, leaving a WORD! or PATH! behind,
// making it suitable for comparisons (e.g. `word = 'foo`)
//
// For generalizing this in Ren-C, the apostrophe was considered a bad choice
// for several reasons.  One is that apostrophe is a valid word character,
// so `'isn't` looked bad...and using it with something in "prime notation"
// (e.g. F' as a variant of F) made it look like a character literal: 'F'.
// It looked bad with '"strings", and if multiple levels of escaping were
// supported then '' looked too much like quote marks.
//
// Hence backslash was chosen to be the generic LITERAL!, a container which
// can be arbitrarily deep.  This faciliated a more succinct way to QUOTE,
// as well as new features:
//
//     >> compose [(1 + 2) \(1 + 2) \\(1 + 2)]
//     == [3 (1 + 2) \(1 + 2)]
//

#include "sys-core.h"

//
//  CT_Literal: C
//
// !!! Currently, in order to have a GENERIC dispatcher (e.g. REBTYPE())
// then one also must implement a comparison function.  However, compare
// functions specifically take REBCEL, so you can't pass a literal to them.
// The handling for LITERAL! is in the comparison dispatch itself.
//
REBINT CT_Literal(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    UNUSED(a); UNUSED(b); UNUSED(mode);
    assert(!"CT_Literal should never be called");
    return 0;
}


//
//  MAKE_Literal: C
//
// MAKE is allowed, but can be done also with UNEVAL (which may also be LIT).
//
// !!! Consider making the others a specialization of MAKE LITERAL! (though it
// would be slightly slower that way.)
//
REB_R MAKE_Literal(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LITERAL);
    UNUSED(kind);

    return Quotify(Move_Value(out, arg), 1);
}


//
//  TO_Literal: C
//
// TO is disallowed at the moment, as there is no clear equivalence of things
// "to" a literal.  (to literal! [[a]] => \\a, for instance?)
//
REB_R TO_Literal(REBVAL *out, enum Reb_Kind kind, const REBVAL *data) {
    UNUSED(out);
    fail (Error_Bad_Make(kind, data));
}


//
//  PD_Literal: C
//
// Historically you could ask a LIT-PATH! questions like its length/etc, just
// like any other path.  So it seems types wrapped in literals should respond
// more or less like their non-literal counterparts...
//
//     >> first quote \[a b c]
//     == a
//
// !!! It might be interesting if the answer were \a instead, adding on a
// level of quotedness that matched the argument...and if arguments had to be
// quoted in order to go the reverse and had the literal levels taken off.
// That would need strong evidence of being useful, however.
//
REB_R PD_Literal(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    UNUSED(picker);
    UNUSED(opt_setval);

    if (KIND_BYTE(pvs->out) == REB_LITERAL)
        Move_Value(pvs->out, KNOWN(pvs->out->payload.literal.cell));
    else {
        assert(KIND_BYTE(pvs->out) >= REB_MAX);
        mutable_KIND_BYTE(pvs->out) %= REB_64;
    }

    // We go through a dispatcher here and use R_REDO_UNCHECKED here because
    // it avoids having to pay for the check of literal types in the general
    // case--the cost is factored in the dispatch.

    return R_REDO_UNCHECKED;
}


//
//  REBTYPE: C
//
// There is no obvious general rule for what a "generic" should do when
// faced with a LITERAL!.  Since they are very new, currently just a fixed
// list of actions are chosen to mean "do whatever the non-literal'd version
// would do, then add the literalness onto the result".
//
//     >> add quote \\\1 2
//     == \\\3
//
// It seems to apply to FIND but not to SELECT, and other oddities.  There
// doesn't seem to be a general rule, so if there's any patterns found they
// should turn into annotations on the generic itself, and are probably
// useful for non-generics as well.
//
REBTYPE(Literal)
{
    REBVAL *literal = D_ARG(1);

    enum Reb_Kind kind = CELL_KIND(VAL_UNESCAPED(literal));
    REBVAL *param = ACT_PARAM(FRM_PHASE(frame_), 1);
    if (not TYPE_CHECK(param, kind))
        fail (Error_Arg_Type(frame_, param, kind));

    REBCNT depth = VAL_LITERAL_DEPTH(literal);

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by val above

        REBSYM prop = VAL_WORD_SYM(ARG(property));
        UNUSED(prop);
        goto unescaped; }

    case SYM_ADD:
    case SYM_SUBTRACT:
    case SYM_MULTIPLY:
    case SYM_DIVIDE:
        //
        // Cool to escape math operators, e.g. \\\10 + 20 => \\\30
        //
        goto escaped;

    case SYM_FIND:
    case SYM_COPY:
    case SYM_SKIP:
    case SYM_AT:
        //
        // Series navigation preserving the level of escaping makes sense
        //
        goto escaped;

    case SYM_APPEND:
    case SYM_CHANGE:
    case SYM_INSERT:
        //
        // Series modification also makes sense.
        //
        goto escaped;

    default:
        goto unescaped;
    }

  unescaped:;
    depth = 0;

  escaped:;

    // Keep the frame, but adjust the pivoting cell to be unescaped.  So
    // either get the contained cell if it's a "real REB_LITERAL", or tweak
    // the type bits back into normal range if a tricky in-cell literal.
    //
    if (KIND_BYTE(literal) == REB_LITERAL)
        Move_Value(D_ARG(1), KNOWN(literal->payload.literal.cell));
    else {
        assert(KIND_BYTE(literal) >= REB_64);
        mutable_KIND_BYTE(D_ARG(1)) %= REB_64;
    }

    REB_R r = Generic_Dispatcher(frame_); // type was checked above

    // It's difficult to interpret an arbitrary REB_R result value for the
    // evaluator (process API values, special requests like REB_R_REDO, etc.)
    //
    // So instead, return the result as normal...but push an integer on the
    // stack that gets processed after the function call is complete.  This
    // fits in with what the Chainer_Dispatcher() does with ACTION!s.  The
    // same code in %c-eval.c that handles that will properly re-literalize
    // the output if needed (as long as it's not a null)
    //
    // !!! Note: A more optimized method might push the REB_LITERAL that we
    // got in, and then check to see if it could reuse the singular series
    // if it had one...though it remains to be seen how much people are using
    // super-deep escaping, and series won't be usually necessary.
    //
    if (depth != 0) {
        DS_PUSH_TRASH;
        Init_Integer(DS_TOP, depth);
    }

    return r;
}


//
//  literal: native/body [
//
//  "Returns value passed in without evaluation"
//
//      return: {The input value, verbatim--unless /SOFT and soft quoted type}
//          [<opt> any-value!]
//      :value {Value to quote, <opt> is impossible (see UNEVAL)}
//          [any-value!]
//      /soft {Evaluate if a GROUP!, GET-WORD!, or GET-PATH!}
//  ][
//      if soft and [match [group! get-word! get-path!] :value] [
//          eval value
//      ] else [
//          :value ;-- also sets unevaluated bit, how could a user do so?
//      ]
//  ]
//
REBNATIVE(literal)
//
// Note: currently aliased in %base-defs.r as LIT and QUOTE.
// QUOTE meaning is slated to be changed, see UNEVAL.
{
    INCLUDE_PARAMS_OF_LITERAL;

    REBVAL *v = ARG(value);

    if (REF(soft) and IS_QUOTABLY_SOFT(v))
        fail ("LITERAL/SOFT not currently implemented, should clone EVAL");

    Move_Value(D_OUT, v);
    SET_VAL_FLAG(D_OUT, VALUE_FLAG_UNEVALUATED);
    return D_OUT;
}


//
//  uneval: native [
//
//  {Constructs a quoted form of the evaluated argument}
//
//      return: "(e.g. makes `\3` from `1 + 2`)"
//          [literal!]
//      optional [<opt> any-value!]
//      /depth "Number of quoting levels to apply (default 1)"
//      count [integer!]
//  ]
//
REBNATIVE(uneval)
//
// UNEVAL is currently slated to be a synonym for QUOTE, once existing uses
// have been changed to LIT or whatever escaping character gets picked (\, %)
{
    INCLUDE_PARAMS_OF_UNEVAL;

    REBINT depth = REF(depth) ? VAL_INT32(ARG(count)) : 1;
    if (depth < 0)
        fail (Error_Invalid(ARG(count)));

    return Quotify(Move_Value(D_OUT, ARG(optional)), depth);
}
