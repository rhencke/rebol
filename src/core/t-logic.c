//
//  File: %t-logic.c
//  Summary: "logic datatype"
//  Section: datatypes
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
#include "sys-deci-funcs.h"


//
//  and?: native [
//
//  {Returns true if both values are conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(and_q)
{
    INCLUDE_PARAMS_OF_AND_Q;

    if (IS_TRUTHY(ARG(value1)) && IS_TRUTHY(ARG(value2)))
        return R_TRUE;

    return R_FALSE;
}


//
//  nor?: native [
//
//  {Returns true if both values are conditionally false (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(nor_q)
{
    INCLUDE_PARAMS_OF_NOR_Q;

    if (IS_FALSEY(ARG(value1)) && IS_FALSEY(ARG(value2)))
        return R_TRUE;

    return R_FALSE;
}


//
//  nand?: native [
//
//  {Returns false if both values are conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(nand_q)
{
    INCLUDE_PARAMS_OF_NAND_Q;

    return R_FROM_BOOL(IS_TRUTHY(ARG(value1)) and IS_TRUTHY(ARG(value2)));
}


//
//  did?: native [
//
//  "Clamps a value to LOGIC! (e.g. a synonym for NOT? NOT? or TO-LOGIC)"
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE and BLANK! for value return FALSE"
//      value [any-value!]
//  ]
//
REBNATIVE(did_q)
{
    INCLUDE_PARAMS_OF_DID_Q;

    return R_FROM_BOOL(IS_TRUTHY(ARG(value)));
}


//
//  did: native/body [
//
//  "Variant of TO-LOGIC which considers null values to also be false"
//
//      return: [logic!]
//          {true if value is NOT a LOGIC! false, BLANK!, or null}
//      optional [<opt> any-value!]
//  ][
//      not not :optional
//  ]
//
REBNATIVE(did)
{
    INCLUDE_PARAMS_OF_DID;

    return R_FROM_BOOL(IS_TRUTHY(ARG(optional)));
}


//
//  not?: native [
//
//  "Returns the logic complement."
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE and BLANK! for value return TRUE"
//      value [any-value!]
//  ]
//
REBNATIVE(not_q)
{
    INCLUDE_PARAMS_OF_NOT_Q;

    return R_FROM_BOOL(IS_FALSEY(ARG(value)));
}


//
//  not: native [
//
//  "Returns the logic complement, considering voids to be false."
//
//      return: [logic!]
//          "Only LOGIC!'s FALSE, BLANK!, and void for cell return TRUE"
//      optional [<opt> any-value!]
//  ]
//
REBNATIVE(not)
{
    INCLUDE_PARAMS_OF_NOT;

    return R_FROM_BOOL(IS_FALSEY(ARG(optional)));
}


//
//  or?: native [
//
//  {Returns true if either value is conditionally true (no "short-circuit")}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(or_q)
{
    INCLUDE_PARAMS_OF_OR_Q;

    return R_FROM_BOOL(IS_TRUTHY(ARG(value1)) or IS_TRUTHY(ARG(value2)));
}


//
//  xor?: native [
//
//  {Returns true if only one of the two values is conditionally true.}
//
//      value1 [any-value!]
//      value2 [any-value!]
//  ]
//
REBNATIVE(xor_q)
{
    INCLUDE_PARAMS_OF_XOR_Q;

    // Note: no boolean ^^ in C; check unequal
    //
    return R_FROM_BOOL(IS_TRUTHY(ARG(value1)) != IS_TRUTHY(ARG(value2)));
}


//
//  and: enfix native [
//
//  {Short-circuit boolean AND, with mode to pass thru non-LOGIC! values}
//
//      return: "LOGIC! if right is GROUP!, else right value or null"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "Quoted expression, evaluated unless left is blank or FALSE"
//          [group! block!]
//  ]
//
REBNATIVE(and)
{
    INCLUDE_PARAMS_OF_AND;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of AND should not be literal block");

    if (IS_GROUP(right)) { // result should be LOGIC!
        if (IS_FALSEY(left))
            Init_Logic(D_OUT, false); // no need to evaluate right
        else if (Do_Any_Array_At_Throws(D_OUT, right))
            return D_OUT;
        else
            Init_Logic(D_OUT, IS_TRUTHY(D_OUT));
        return D_OUT;
    }

    assert(IS_BLOCK(right)); // any-value! result, or null

    if (IS_FALSEY(left))
        return nullptr; // no need to evaluate right

    if (Do_Any_Array_At_Throws(D_OUT, right))
        return D_OUT;

    if (IS_FALSEY(D_OUT))
        return nullptr;

    return D_OUT; // preserve the exact truthy value
}


//  or: enfix native [
//
//  {Short-circuit boolean OR, with mode to pass thru non-LOGIC! values}
//
//      return: "LOGIC! if right is GROUP!, truthy left/right value, or null"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "Quoted expression, evaluated only if left is blank or FALSE"
//          [group! block!]
//  ]
REBNATIVE(or)
{
    INCLUDE_PARAMS_OF_OR;

    REBVAL *left = ARG(left);
    REBVAL *right = ARG(right);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of OR should not be literal block");

    if (IS_GROUP(right)) { // result should be LOGIC!
        if (IS_TRUTHY(left))
            Init_Logic(D_OUT, true); // no need to evaluate right
        else if (Do_Any_Array_At_Throws(D_OUT, right))
            return D_OUT;
        else
            Init_Logic(D_OUT, IS_TRUTHY(D_OUT));
        return D_OUT;
    }

    assert(IS_BLOCK(right)); // any-value! result, or null

    if (IS_TRUTHY(left))
        return left; // no need to evaluate right

    if (Do_Any_Array_At_Throws(D_OUT, right))
        return D_OUT;

    if (IS_TRUTHY(D_OUT))
        return D_OUT; // preserve the exact truthy value

    return nullptr;
}


//
//  xor: enfix native [
//
//  {Boolean XOR, with mode to pass thru non-LOGIC! values}
//
//      return: "LOGIC! if right is GROUP!, else left or right or null"
//          [<opt> any-value!]
//      left "Expression which will always be evaluated"
//          [<opt> any-value!]
//      :right "Quoted expression, must be always evaluated as well"
//          [group! block!]
//  ]
//
REBNATIVE(xor)
{
    INCLUDE_PARAMS_OF_XOR;

    REBVAL *left = ARG(left);

    if (IS_BLOCK(left) and GET_VAL_FLAG(left, VALUE_FLAG_UNEVALUATED))
        fail ("left hand side of XOR should not be literal block");

    if (Do_Any_Array_At_Throws(D_OUT, ARG(right))) // always evaluated
        return D_OUT;

    REBVAL *right = D_OUT;

    if (IS_GROUP(right)) { // result should be LOGIC!
        Init_Logic(D_OUT, IS_TRUTHY(left) != IS_TRUTHY(right));
        return D_OUT;
    }

    assert(IS_BLOCK(right)); // any-value! result, or null

    if (IS_FALSEY(left)) {
        if (IS_FALSEY(right))
            return nullptr;
        return right;
    }

    if (IS_TRUTHY(right))
        return nullptr;

    return left;
}


//
//  CT_Logic: C
//
REBINT CT_Logic(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0)  return (VAL_LOGIC(a) == VAL_LOGIC(b));
    return -1;
}


//
//  MAKE_Logic: C
//
void MAKE_Logic(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a construction routine, MAKE takes more liberties in the
    // meaning of its parameters, so it lets zero values be false.
    //
    // !!! Is there a better idea for MAKE that does not hinge on the
    // "zero is false" concept?  Is there a reason it should?
    //
    if (
        IS_FALSEY(arg)
        || (IS_INTEGER(arg) && VAL_INT64(arg) == 0)
        || (
            (IS_DECIMAL(arg) || IS_PERCENT(arg))
            && (VAL_DECIMAL(arg) == 0.0)
        )
        || (IS_MONEY(arg) && deci_is_zero(VAL_MONEY_AMOUNT(arg)))
    ) {
        Init_Logic(out, FALSE);
    }
    else
        Init_Logic(out, TRUE);
}


//
//  TO_Logic: C
//
void TO_Logic(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg) {
    assert(kind == REB_LOGIC);
    UNUSED(kind);

    // As a "Rebol conversion", TO falls in line with the rest of the
    // interpreter canon that all non-none non-logic-false values are
    // considered effectively "truth".
    //
    Init_Logic(out, IS_TRUTHY(arg));
}


static inline REBOOL Math_Arg_For_Logic(REBVAL *arg)
{
    if (IS_LOGIC(arg))
        return VAL_LOGIC(arg);

    if (IS_BLANK(arg))
        return FALSE;

    fail (Error_Unexpected_Type(REB_LOGIC, VAL_TYPE(arg)));
}


//
//  MF_Logic: C
//
void MF_Logic(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form); // currently no distinction between MOLD and FORM

    Emit(mo, "+N", VAL_LOGIC(v) ? Canon(SYM_TRUE) : Canon(SYM_FALSE));
}


//
//  REBTYPE: C
//
REBTYPE(Logic)
{
    REBOOL val1 = VAL_LOGIC(D_ARG(1));
    REBOOL val2;

    switch (VAL_WORD_SYM(verb)) {

    case SYM_INTERSECT:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = (val1 and val2);
        break;

    case SYM_UNION:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = (val1 or val2);
        break;

    case SYM_DIFFERENCE:
        val2 = Math_Arg_For_Logic(D_ARG(2));
        val1 = (val1 != val2);
        break;

    case SYM_COMPLEMENT:
        val1 = not val1;
        break;

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            // random/seed false restarts; true randomizes
            Set_Random(val1 ? cast(REBINT, OS_DELTA_TIME(0)) : 1);
            return nullptr;
        }
        if (Random_Int(REF(secure)) & 1)
            return R_TRUE;
        return R_FALSE; }

    default:
        fail (Error_Illegal_Action(REB_LOGIC, verb));
    }

    return val1 ? R_TRUE : R_FALSE;
}
