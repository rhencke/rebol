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
// !!! `(quote 'a) = (quote a)` is true in historical Rebol, due to the
// rules of "lax equality".  These rules are up in the air as they pertain
// to the IS and ISN'T transition.
//
// !!! How these comparisons are supposed to work is a mystery, but integer
// does it like:
//
//     if (mode >= 0)  return (VAL_INT64(a) == VAL_INT64(b));
//     if (mode == -1) return (VAL_INT64(a) >= VAL_INT64(b));
//     return (VAL_INT64(a) > VAL_INT64(b));
//
REBINT CT_Literal(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode < 0)
        fail ("LITERAL! currently only implements equality testing");

    if (VAL_LITERAL_DEPTH(a) != VAL_LITERAL_DEPTH(b))
        return 0;

    a = VAL_UNESCAPED(a);
    b = VAL_UNESCAPED(b);

    bool is_case = (mode == 1);
    return (Cmp_Value(a, b, is_case) == 0) ? 1 : 0;
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

    return Init_Literal(out, arg);
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
//  MF_Literal: C
//
// Molding just puts the number of backslashes before the item that it has.
//
void MF_Literal(REB_MOLD *mo, const RELVAL *v, bool form)
{
    // !!! There is currently no distinction between MOLD and FORM, but:
    //
    //      print ["What should this print?:" quote \\\"something"]
    //
    UNUSED(form);

    REBCNT depth = VAL_LITERAL_DEPTH(v);
    REBCNT i;
    for (i = 0; i < depth; ++i)
        Append_Unencoded(mo->series, "\\");

    const RELVAL *wrap = VAL_UNESCAPED(v);
    if (not IS_NULLED(wrap)) {
        bool real_form = false; // don't ever "form"
        Mold_Or_Form_Value(mo, wrap, real_form);
    }
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

    Move_Value(pvs->out, VAL_UNESCAPED(pvs->out));

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
    REBVAL *val = D_ARG(1);

    REBCNT depth = VAL_ESCAPE_DEPTH(val);
    Move_Value(val, VAL_UNESCAPED(val));

    enum Reb_Kind kind = VAL_TYPE(val);
    REBVAL *param = ACT_PARAM(FRM_PHASE(frame_), 1);
    if (not TYPE_CHECK(param, kind))
        fail (Error_Arg_Type(frame_, param, kind));

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
//  literal: native [
//
//  {Constructs a literal form of the given value (e.g. makes `\x` from `x`)}
//
//      return: [literal!]
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(literal)
//
// Note: currently aliased in %base-defs.r as LIT and UNEVAL.
{
    INCLUDE_PARAMS_OF_LITERAL;

    return Init_Literal(D_OUT, ARG(optional));
}
