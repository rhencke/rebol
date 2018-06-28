//
//  File: %sys-array.h
//  Summary: {Definitions for REBARR}
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
// A "Rebol Array" is a series of REBVAL values which is terminated by an
// END marker.  In R3-Alpha, the END marker was itself a full-sized REBVAL
// cell...so code was allowed to write one cell past the capacity requested
// when Make_Array() was called.  But this always had to be an END.
//
// In Ren-C, there is an implicit END marker just past the last cell in the
// capacity.  Allowing a SET_END() on this position could corrupt the END
// signaling slot, which only uses a bit out of a Reb_Header sized item to
// signal.  Use TERM_ARRAY_LEN() to safely terminate arrays and respect not
// writing if it's past capacity.
//
// While many operations are shared in common with REBSER, there is a
// (deliberate) type incompatibility introduced.  The type compatibility is
// implemented in a way that works in C or C++ (though it should be reviewed
// for strict aliasing compliance).  To get the underlying REBSER of a REBARR
// use the SER() operation.
//
// An ARRAY is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).
//


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  An
// empty array should have an END marker in its head slot, and since it has
// no last value then ARR_LAST should not be called (this is checked in
// debug builds).  A fully constructed array should always have an END
// marker in its tail slot, which is one past the last position that is
// valid for writing a full REBVAL.

inline static RELVAL *ARR_AT(REBARR *a, REBCNT n)
    { return SER_AT(RELVAL, cast(REBSER*, a), n); }

inline static RELVAL *ARR_HEAD(REBARR *a)
    { return SER_HEAD(RELVAL, cast(REBSER*, a)); }

inline static RELVAL *ARR_TAIL(REBARR *a)
    { return SER_TAIL(RELVAL, cast(REBSER*, a)); }

inline static RELVAL *ARR_LAST(REBARR *a)
    { return SER_LAST(RELVAL, cast(REBSER*, a)); }

// If you know something is a singular array a priori, then you don't have to
// check the SERIES_INFO_HAS_DYNAMIC as you would in a generic ARR_HEAD.
//
inline static RELVAL *ARR_SINGLE(REBARR *a) {
    assert(NOT_SER_INFO(a, SERIES_INFO_HAS_DYNAMIC));
    return cast(RELVAL*, &SER(a)->content.fixed.values[0]);
}

// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
inline static REBARR *Singular_From_Cell(const RELVAL *v) {
    REBARR *singular = ARR( // some checking in debug builds is done by ARR()
        cast(void*,
            cast(REBYTE*, m_cast(RELVAL*, v))
            - offsetof(struct Reb_Series, content)
        )
    );
    assert(NOT_SER_INFO(singular, SERIES_INFO_HAS_DYNAMIC));
    return singular;
}

// As with an ordinary REBSER, a REBARR has separate management of its length
// and its terminator.  Many routines seek to choose the precise moment to
// sync these independently for performance reasons (for better or worse).
//
#define ARR_LEN(a) \
    SER_LEN(SER(a))


// TERM_ARRAY_LEN sets the length and terminates the array, and to get around
// the problem it checks to see if the length is the rest - 1.  Another
// possibility would be to check to see if the cell was already marked with
// END...however, that would require initialization of all cells in an array
// up front, to legitimately examine the bits (and decisions on how to init)
//
inline static void TERM_ARRAY_LEN(REBARR *a, REBCNT len) {
    REBCNT rest = SER_REST(SER(a));
    assert(len < rest);
    SET_SERIES_LEN(SER(a), len);
    if (len + 1 == rest)
        assert(IS_END(ARR_TAIL(a)));
    else
        SET_END(ARR_TAIL(a));
}

inline static void SET_ARRAY_LEN_NOTERM(REBARR *a, REBCNT len) {
    SET_SERIES_LEN(SER(a), len); // call out non-terminating usages
}

inline static void RESET_ARRAY(REBARR *a) {
    TERM_ARRAY_LEN(a, 0);
}

inline static void TERM_SERIES(REBSER *s) {
    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
        TERM_ARRAY_LEN(ARR(s), SER_LEN(s));
    else
        memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}


// Setting and getting array flags is common enough to want a macro for it
// vs. having to extract the ARR_SERIES to do it each time.
//
#define IS_ARRAY_MANAGED(a) \
    IS_SERIES_MANAGED(SER(a))

#define MANAGE_ARRAY(a) \
    MANAGE_SERIES(SER(a))

#define ENSURE_ARRAY_MANAGED(a) \
    ENSURE_SERIES_MANAGED(SER(a))

#define PUSH_GUARD_ARRAY(a) \
    PUSH_GUARD_SERIES(SER(a))

#define DROP_GUARD_ARRAY(a) \
    DROP_GUARD_SERIES(SER(a))

inline static void PUSH_GUARD_ARRAY_CONTENTS(REBARR *a) {
    assert(not IS_ARRAY_MANAGED(a)); // if managed, just use PUSH_GUARD_ARRAY
    Guard_Node_Core(NOD(a));
}

inline static void DROP_GUARD_ARRAY_CONTENTS(REBARR *a) {
    DROP_GUARD_SERIES(SER(a));
}


//
// Locking
//

inline static REBOOL Is_Array_Deeply_Frozen(REBARR *a) {
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

#define FAIL_IF_READ_ONLY_ARRAY(a) \
    FAIL_IF_READ_ONLY_SERIES(SER(a))


// Make a series that is the right size to store REBVALs (and marked for the
// garbage collector to look into recursively).  ARR_LEN() will be 0.
//
inline static REBARR *Make_Array_Core(REBCNT capacity, REBFLGS flags) {
    const REBCNT wide = sizeof(REBVAL);

    REBSER *s = Make_Series_Node(wide, SERIES_FLAG_ARRAY | flags);

    if (capacity > 1) {
        capacity += 1; // account for cell needed for terminator (END)

        // Don't pay for oversize check unless dynamic.  It means the node
        // that just got allocated may get GC'd/freed...that's insignificant.
        //
        if (cast(REBU64, capacity) * wide > INT32_MAX)
            fail (Error_No_Memory(cast(REBU64, capacity) * wide));

        if (not Did_Series_Data_Alloc(s, capacity))
            fail (Error_No_Memory(capacity * wide));

      #if !defined(NDEBUG)
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif

      TERM_ARRAY_LEN(cast(REBARR*, s), 0); // (non-dynamic is auto-terminated)
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (flags & ARRAY_FLAG_FILE_LINE) {
        if (
            TG_Frame_Stack and TG_Frame_Stack->source.array and
            GET_SER_FLAG(TG_Frame_Stack->source.array, ARRAY_FLAG_FILE_LINE)
        ){
            LINK(s).file = LINK(TG_Frame_Stack->source.array).file;
            MISC(s).line = MISC(TG_Frame_Stack->source.array).line;
        }
        else
            CLEAR_SER_FLAG(s, ARRAY_FLAG_FILE_LINE);
    }

    assert(ARR_LEN(cast(REBARR*, s)) == 0);
    return cast(REBARR*, s);
}

#define Make_Array(capacity) \
    Make_Array_Core((capacity), ARRAY_FLAG_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Array() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
inline static REBARR *Make_Array_For_Copy(
    REBCNT capacity,
    REBFLGS flags,
    REBARR *original
){
    if (original and GET_SER_FLAG(original, ARRAY_FLAG_TAIL_NEWLINE)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_TAIL_NEWLINE;
    }

    if (
        (flags & ARRAY_FLAG_FILE_LINE)
        and (original and GET_SER_FLAG(original, ARRAY_FLAG_FILE_LINE))
    ){
        flags &= ~ARRAY_FLAG_FILE_LINE;

        REBARR *a = Make_Array_Core(capacity, flags);
        LINK(a).file = LINK(original).file;
        MISC(a).line = MISC(original).line;
        SET_SER_FLAG(a, ARRAY_FLAG_FILE_LINE);
        return a;
    }

    return Make_Array_Core(capacity, flags);
}


// A singular array is specifically optimized to hold *one* value in a REBSER
// node directly, and stay fixed at that size.
//
// Note ARR_SINGLE() must be overwritten by the caller...it contains an END
// marker but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need SERIES_FLAG_FILE_LINE.
//
inline static REBARR *Alloc_Singular(REBFLGS flags) {
    REBARR *a = Make_Array_Core(1, flags | SERIES_FLAG_FIXED_SIZE);
    THIRD_BYTE(SER(a)->info) = 1; // non-dynamic length (defaulted to 0)
    return a;
}

#define Append_Value(a,v) \
    Move_Value(Alloc_Tail_Array(a), (v))

#define Append_Value_Core(a,v,s) \
    Derelativize(Alloc_Tail_Array(a), (v), (s))


#define Copy_Values_Len_Shallow(v,s,l) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), 0, 0)

#define Copy_Values_Len_Shallow_Core(v,s,l,f) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), 0, (f))

#define Copy_Values_Len_Extra_Shallow(v,s,l,e) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), (e), 0) 


#define Copy_Array_Shallow(a,s) \
    Copy_Array_At_Shallow((a), 0, (s))

#define Copy_Array_Shallow_Flags(a,s,f) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), 0, (f))

#define Copy_Array_Deep_Managed(a,s) \
    Copy_Array_At_Extra_Deep_Managed((a), 0, (s), 0)

#define Copy_Array_At_Deep_Managed(a,i,s) \
    Copy_Array_At_Extra_Deep_Managed((a), (i), (s), 0)

#define COPY_ANY_ARRAY_AT_DEEP_MANAGED(v) \
    Copy_Array_At_Extra_Deep_Managed( \
        VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), 0)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0, SERIES_FLAGS_NONE)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e), SERIES_FLAGS_NONE)

// See TS_NOT_COPIED for the default types excluded from being deep copied
//
inline static REBARR* Copy_Array_At_Extra_Deep_Managed(
    REBARR *original,
    REBCNT index,
    REBSPC *specifier,
    REBCNT extra
){
    return Copy_Array_Core_Managed(
        original,
        index, // at
        specifier,
        ARR_LEN(original), // tail
        extra, // extra
        SERIES_FLAGS_NONE, // no ARRAY_FLAG_FILE_LINE by default
        TS_SERIES & ~TS_NOT_COPIED // types
    );
}

#define Free_Unmanaged_Array(a) \
    Free_Unmanaged_Series(SER(a))



//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-ARRAY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %sys-bind.h
//

#define EMPTY_BLOCK \
    Root_Empty_Block

#define EMPTY_ARRAY \
    PG_Empty_Array // Note: initialized from VAL_ARRAY(Root_Empty_Block)

#define EMPTY_STRING \
    Root_Empty_String


#ifdef NDEBUG
    #define SPC(p) \
        cast(REBSPC*, (p)) // makes UNBOUND look like SPECIFIED

    #define VAL_SPECIFIER(v) \
        SPC(v->extra.binding)
#else
    inline static REBSPC* SPC(void *p) {
        REBNOD *specifier = NOD(p);

      #if !defined(NDEBUG)
        if (not (specifier->header.bits & NODE_FLAG_MANAGED)) {
            REBFRM *f = FRM(specifier);
            assert(f->eval_type == REB_ACTION);
        }
        else if (not (specifier->header.bits & ARRAY_FLAG_VARLIST)) {
            assert(specifier == SPECIFIED);
        }
      #endif

        return cast(REBSPC*, specifier);
    }

    inline static REBSPC *VAL_SPECIFIER(const REBVAL *v) {
        assert(VAL_TYPE(v) == REB_0_REFERENCE or ANY_ARRAY(v));
        if (v->extra.binding == UNBOUND)
            return SPECIFIED;

        // While an ANY-WORD! can be bound specifically to an arbitrary
        // object, an ANY-ARRAY! only becomes bound specifically to frames.
        // The keylist for a frame's context should come from a function's
        // paramlist, which should have an ACTION! value in keylist[0]
        //
        REBCTX *c = CTX(v->extra.binding);
        assert(GET_SER_FLAG(c, NODE_FLAG_STACK));
        return cast(REBSPC*, c);
    }
#endif

inline static void INIT_VAL_ARRAY(RELVAL *v, REBARR *a) {
    INIT_BINDING(v, UNBOUND);
    assert(IS_ARRAY_MANAGED(a));
    v->payload.any_series.series = SER(a);
}

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v) \
    ARR_AT(VAL_ARRAY(v), VAL_INDEX(v))

#define VAL_ARRAY_LEN_AT(v) \
    VAL_LEN_AT(v)

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static REBARR *VAL_ARRAY(const RELVAL *v) {
    assert(ANY_ARRAY(v));
    return ARR(v->payload.any_series.series);
}

#define VAL_ARRAY_HEAD(v) \
    ARR_HEAD(VAL_ARRAY(v))

inline static RELVAL *VAL_ARRAY_TAIL(const RELVAL *v) {
    return ARR_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v));
}


// !!! VAL_ARRAY_AT_HEAD() is a leftover from the old definition of
// VAL_ARRAY_AT().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted rom the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
#define VAL_ARRAY_AT_HEAD(v,n) \
    ARR_AT(VAL_ARRAY(v), (n))

#define Init_Any_Array_At(v,t,a,i) \
    Init_Any_Series_At((v), (t), SER(a), (i))

#define Init_Any_Array(v,t,a) \
    Init_Any_Array_At((v), (t), (a), 0)

#define Init_Block(v,s) \
    Init_Any_Array((v), REB_BLOCK, (s))

#define Init_Group(v,s) \
    Init_Any_Array((v), REB_GROUP, (s))

#define Init_Path(v,s) \
    Init_Any_Array((v), REB_PATH, (s))



#ifdef NDEBUG
    #define ASSERT_ARRAY(s) \
        NOOP

    #define ASSERT_ARRAY_MANAGED(array) \
        NOOP

    #define ASSERT_SERIES(s) \
        NOOP
#else
    #define ASSERT_ARRAY(s) \
        Assert_Array_Core(s)

    #define ASSERT_ARRAY_MANAGED(array) \
        ASSERT_SERIES_MANAGED(SER(array))

    static inline void ASSERT_SERIES(REBSER *s) {
        if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
            Assert_Array_Core(ARR(s));
        else
            Assert_Series_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (ARR_LEN(a) != 0 and (v) >= ARR_HEAD(a) and (v) < ARR_TAIL(a))
#endif
