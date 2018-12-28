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
// HOWEVER... there is an efficiency trick, which uses the VAL_TYPE_RAW()
// byte div 4 as the "lit level" of a value.  Then the byte mod 4 becomes the
// actual type.  So only an actual REB_LITERAL at "apparent lit-level 0" has
// its own payload...as a last resort if the level exceeded what the type byte
// can encode.  This saves on storage and GC load for small levels of
// literalness, at the cost of making VAL_TYPE() do an extra comparison to
// clip all values above 64 to act as REB_LITERAL.
//

inline static REBCNT VAL_LITERAL_DEPTH(const RELVAL *v) {
    if (KIND_BYTE(v) >= REB_64) // shallow enough to use type byte trick...
        return KIND_BYTE(v) / REB_64; // ...see explanation above
    assert(KIND_BYTE(v) == REB_LITERAL);
    return v->payload.literal.depth;
}

inline static REBCNT VAL_NUM_QUOTES(const RELVAL *v) {
    if (not IS_LITERAL(v))
        return 0;
    return VAL_LITERAL_DEPTH(v);
}

// It is necessary to be able to store relative values in escaped cells.
//
inline static RELVAL *Quotify_Core(
    RELVAL *v,
    REBCNT depth
){
    if (KIND_BYTE(v) == REB_LITERAL) { // reuse payload, bump count
        assert(v->payload.literal.depth > 3); // or should've used kind byte
        v->payload.literal.depth += depth;
        return v;
    }

    enum Reb_Kind kind = cast(enum Reb_Kind, KIND_BYTE(v) % REB_64);
    depth += KIND_BYTE(v) / REB_64;

    if (depth <= 3) { // can encode in a cell with no REB_LITERAL payload
        mutable_KIND_BYTE(v) = kind + (REB_64 * depth);
    }
    else {
        // No point having ARRAY_FLAG_FILE_LINE when only deep levels of a
        // literal would have it--wastes time/storage to save it.
        //
        // !!! Efficiency trick here could point to VOID_VALUE, BLANK_VALUE,
        // NULLED_CELL, etc. in those cases, so long as GC knew.
        //
        REBARR *a = Alloc_Singular(
            NODE_FLAG_MANAGED | ARRAY_FLAG_NULLEDS_LEGAL
        );

        // This is an uncomfortable situation of moving values without a
        // specifier; but it needs to be done otherwise you could not have
        // literals in function bodies.  What it means is that you should
        // not be paying attention to the cell bits for making decisions
        // about specifiers and such.  The format bits of this cell are
        // essentially noise, and only the literal's specifier should be used.

        RELVAL *cell = ARR_SINGLE(a);
        Move_Value_Header(cell, v);
        mutable_KIND_BYTE(cell) = kind; // escaping only in literal
        cell->extra = v->extra;
        cell->payload = v->payload;
      #if !defined(NDEBUG)
        SET_VAL_FLAG(cell, CELL_FLAG_PROTECTED); // maybe shared; can't change
      #endif
 
        RESET_VAL_HEADER(v, REB_LITERAL);
        if (Is_Bindable(cell))
            v->extra = cell->extra; // must be in sync with cell (if binding)
        else {
            // We say all REB_LITERAL cells are bindable, so their binding gets
            // checked even if the contained cell isn't bindable.  By setting
            // the binding to null if the contained cell isn't bindable, that
            // prevents needing to make Is_Bindable() a more complex check,
            // we can just say yes always but have the binding null if not.
            //
            v->extra.binding = nullptr;
        }
        v->payload.literal.cell = cell;
        v->payload.literal.depth = depth;

      #if !defined(NDEBUG) && defined(DEBUG_COUNT_TICKS)
        //
        // Throw in a little corruption just to throw a wrench into anyone
        // who might be checking flags on a literal.
        //
        // !!! Would it perform better to store the depth here instead of
        // the payload?  Limiting to 256 levels of escaping doesn't seem
        // that prohibitive.
        //
        mutable_CUSTOM_BYTE(v) = TG_Tick % 256;
      #endif
    }

    return v;
}

#if !defined(CPLUSPLUS_11)
    #define Quotify Quotify_Core
#else
    inline static REBVAL *Quotify(REBVAL *v, REBCNT depth)
        { return KNOWN(Quotify_Core(v, depth)); }

    inline static RELVAL *Quotify(RELVAL *v, REBCNT depth)
        { return Quotify_Core(v, depth); }
#endif


// Turns `\x` into `x`, or `\\\[1 + 2]` into `\\(1 + 2)`, etc.
//
inline static RELVAL *Unquotify_Core(
    RELVAL *v,
    REBCNT unquotes
){
    if (KIND_BYTE(v) != REB_LITERAL) {
        assert(KIND_BYTE(v) > REB_64); // can't unliteralize a non-literal
        assert(cast(REBCNT, KIND_BYTE(v) / REB_64) >= unquotes);
        mutable_KIND_BYTE(v) -= REB_64 * unquotes;
        return v;
    }

    REBCNT depth = v->payload.literal.depth;
    assert(depth > 3 and depth >= unquotes);
    depth -= unquotes;

    RELVAL *cell = v->payload.literal.cell;
    assert(KIND_BYTE(cell) != REB_LITERAL and KIND_BYTE(cell) < REB_64);

    if (depth > 3) { // unescaped can't encode in single value
        v->payload.literal.depth = depth;
    }
    else {
        Move_Value_Header(v, cell);
        mutable_KIND_BYTE(v) += (REB_64 * depth);
        assert(
            not Is_Bindable(cell)
            or v->extra.binding == cell->extra.binding // must be in sync
        );
        v->payload = cell->payload;
    }
    return v;
}

#if !defined(CPLUSPLUS_11)
    #define Unquotify Unquotify_Core
#else
    inline static REBVAL *Unquotify(REBVAL *v, REBCNT depth)
        { return KNOWN(Unquotify_Core(v, depth)); }

    inline static RELVAL *Unquotify(RELVAL *v, REBCNT depth)
        { return Unquotify_Core(v, depth); }
#endif


inline static const REBCEL *VAL_UNESCAPED(const RELVAL *v) {
    if (KIND_BYTE(v) != REB_LITERAL)
        return v; // kind byte may be > 64

    // The reason this routine returns `const` is because you can't modify
    // the contained value without affecting other views of it, if it is
    // shared in an escaping.  Modifications must be done with awareness of
    // the original RELVAL, and that it might be a LITERAL!.
    //
    return v->payload.literal.cell;
}


inline static REBCNT Dequotify(RELVAL *v) {
    if (KIND_BYTE(v) != REB_LITERAL) {
        REBCNT depth = KIND_BYTE(v) / REB_64;
        mutable_KIND_BYTE(v) %= REB_64;
        return depth;
    }

    REBCNT depth = v->payload.literal.depth;
    RELVAL *cell = v->payload.literal.cell;
    assert(KIND_BYTE(cell) != REB_LITERAL and KIND_BYTE(cell) < REB_64);

    Move_Value_Header(v, cell);
  #if !defined(NDEBUG)
    if (Is_Bindable(cell))
        assert(v->extra.binding == cell->extra.binding);
    else
        assert(not v->extra.binding);
  #endif
    v->extra = cell->extra;
    v->payload = cell->payload;
    return depth;
}
