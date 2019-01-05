//
//  File: %sys-protect.h
//  Summary: "System Const and Protection Functions"
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
// R3-Alpha introduced the idea of "protected" series and variables.  Ren-C
// introduces a new form of read-only-ness that is not a bit on series, but
// rather bits on values.  This means that a value can be a read-only view of
// a series that is otherwise mutable.
//
// !!! Checking for read access was a somewhat half-baked feature in R3-Alpha,
// as heeding the protection bit had to be checked explicitly.  Many places in
// the code did not do the check.  While several bugs of that nature have
// been replaced in an ad-hoc fashion, a better solution would involve using
// C's `const` feature to locate points that needed to promote series access
// to be mutable, so it could be checked at compile-time.
//

inline static void FAIL_IF_READ_ONLY_SERIES_CORE(
    RELVAL *series,
    REBSPC *specifier
){
    REBSER *s = series->payload.any_series.series;
    FAIL_IF_READ_ONLY_SER(s);
    if (GET_CELL_FLAG(series, CONST)) {
        DECLARE_LOCAL (specific);
        Derelativize(specific, series, specifier);
        fail (Error_Const_Value_Raw(specific));
    }
}

#define FAIL_IF_READ_ONLY_SERIES(series) \
    FAIL_IF_READ_ONLY_SERIES_CORE((series), SPECIFIED)



inline static bool Is_Array_Deeply_Frozen(REBARR *a) {
    return GET_SER_INFO(a, SERIES_INFO_FROZEN);

    // should be frozen all the way down (can only freeze arrays deeply)
}

inline static void Deep_Freeze_Array(REBARR *a) {
    Protect_Series(
        SER(a),
        0, // start protection at index 0
        PROT_DEEP | PROT_SET | PROT_FREEZE
    );
    Uncolor_Array(a);
}

#define Is_Array_Shallow_Read_Only(a) \
    Is_Series_Read_Only(a)

inline static void FAIL_IF_READ_ONLY_ARRAY_CORE(
    RELVAL *array,
    REBSPC *specifier
){
    assert(ANY_ARRAY(array));
    FAIL_IF_READ_ONLY_SERIES_CORE(array, specifier);
}

#define FAIL_IF_READ_ONLY_ARRAY(array) \
    FAIL_IF_READ_ONLY_ARRAY_CORE((array), SPECIFIED)


inline static void FAIL_IF_READ_ONLY_CONTEXT(RELVAL *context) {
    assert(ANY_CONTEXT(context));
    REBARR *varlist = context->payload.any_context.varlist;
    FAIL_IF_READ_ONLY_SER(SER(varlist));

    // !!! CONST is a work in progress, experimental, but would need handling
    // here too...
}


inline static void FAIL_IF_READ_ONLY_VALUE(RELVAL *v) {
    if (ANY_SERIES(v))
        FAIL_IF_READ_ONLY_SERIES(v);
    else if (ANY_CONTEXT(v))
        FAIL_IF_READ_ONLY_CONTEXT(v);
    else if (ANY_SCALAR(v) or IS_BLANK(v))
        fail ("Scalars are immutable");
    else if (ANY_PATH(v))
        fail ("Paths are immutable");
}
