//
//  File: %sys-literal.h
//  Summary: {Definitions for Literal Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Rebol Open Source Contributors
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
// In Ren-C, any value can be "lit" escaped, any number of times.  Since there
// is no limit to how many levels of escaping there can be, the general case
// of the escaping cannot fit in a value cell, so a "singular" array is used
// (a compact form with only a series tracking node, sizeof(REBVAL)*2)
//
// !!! Note: There is a planned efficiency trick, which would use the type
// byte div 4 as the "lit level" of a value.  Then the byte mod 4 would be
// the actual type.  At that point, only a LITERAL! at lit-level 0 would
// have a payload...which it would use only if the level exceeded what the
// type byte would encode.  This would save on storage and GC load for small
// levels of literalness, at the cost of making VAL_TYPE() cost a bit more.
// (Unfortunate as it is called often, but one really wants to avoid making
// series for what appear to be atomic values if one can help it.)
//

inline static REBCNT VAL_LITERAL_DEPTH(const RELVAL *v) {
    assert(IS_LITERAL(v));
    return v->payload.literal.depth;
}

inline static REBCNT VAL_ESCAPE_DEPTH(const RELVAL *v) {
    if (not IS_LITERAL(v))
        return 0;
    return v->payload.literal.depth;
}


inline static REBVAL* Init_Literal_Nulled(RELVAL *out, REBCNT depth)
{
    REBARR *a = Alloc_Singular(NODE_FLAG_MANAGED | ARRAY_FLAG_NULLEDS_LEGAL);
    Init_Nulled(ARR_SINGLE(a));
    RESET_CELL(out, REB_LITERAL);
    out->extra.trash = nullptr;
    out->payload.literal.singular = a;
    out->payload.literal.depth = depth;

    return KNOWN(out);
}

inline static REBVAL *Init_Escaped(
    RELVAL *out,
    const REBVAL *wrap, // Note: can be same as out pointer
    REBCNT depth
){
    if (depth == 0) { // returning plain value
        if (out != wrap)
            Move_Value(out, wrap);
    }
    else if (IS_LITERAL(wrap)) { // can reuse series, just bump count.
        if (out != wrap)
            Move_Value(out, wrap);
        out->payload.literal.depth += depth;
    }
    else {
        // No point having ARRAY_FLAG_FILE_LINE when only deep levels of a
        // literal would have it--wastes time/storage to save it.
        //
        REBARR *a = Alloc_Singular(
            NODE_FLAG_MANAGED | ARRAY_FLAG_NULLEDS_LEGAL
        );
        Move_Value(ARR_SINGLE(a), wrap);

        RESET_CELL(out, REB_LITERAL);
        out->extra.trash = nullptr;
        out->payload.literal.singular = a;
        out->payload.literal.depth = depth;
    }

    return KNOWN(out);
}

#define Init_Literal(out, wrap) \
    Init_Escaped((out), (wrap), 1) // add one level of escaping


// Turns `\x` into `x`, or `\\\x` into `\\x`, etc.
//
inline static REBVAL *Unliteralize(
    RELVAL *out,
    const RELVAL *v, // Note: May be same pointer as out (used by pathing)
    REBSPC *specifier
){
    assert(IS_LITERAL(v));
    assert(v->payload.literal.depth != 0);
    if (v->payload.literal.depth == 1) {
        RELVAL *wrap = ARR_SINGLE(v->payload.literal.singular);
        Derelativize(out, wrap, specifier);
    }
    else {
        if (out != v)
            Derelativize(out, v, specifier);
        --out->payload.literal.depth;
    }
    return KNOWN(out);
}
