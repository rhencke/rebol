//
//  File: %sys-string.h
//  Summary: {Definitions for REBSTR (e.g. WORD!) and REBUNI (e.g. STRING!)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha and Red worked with strings in their decoded form, in series with
// fixed-size elements of varying width (Latin1, UTF-16).  Ren-C goes instead
// with the idea of "UTF-8 everywhere", storing all words and strings as
// UTF-8, and only converting at I/O points if the platform requires it
// (e.g. Windows).  Rationale for this methodlogy is outlined here:
//
// http://utf8everywhere.org/
//
// UTF-8 strings are "byte-sized series", which is also true of BINARY!
// datatypes.  However, the series used to store UTF-8 strings also store
// information about their length in codepoints in their series nodes (the
// main "number of bytes used" in the series conveys bytes, not codepoints).
//


//=////////////////////////////////////////////////////////////////////////=//
//
// REBCHR(*) + REBCHR(const *): "ITERATOR" TYPE FOR KNOWN GOOD UTF-8 DATA
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol exchanges UTF-8 data with the outside world via "char*".  But inside
// the code, REBYTE* is used for not-yet-validated bytes that are to be
// scanned as UTF-8.  When accessing an already-checked string, however,
// the REBCHR(*) type is used...signaling no error checking should need to be
// done while walking through the UTF-8 sequence.
//
// So for instance: instead of simply saying:
//
//     REBUNI *ptr = UNI_HEAD(string_series);
//     REBUNI c = *ptr++;
//
// ...one must instead write:
//
//     REBCHR(*) ptr = UNI_HEAD(string_series);
//     ptr = NEXT_CHR(&c, ptr);  // ++ptr or ptr[n] will error in C++ build
//
// The code that runs behind the scenes is typical UTF-8 forward and backward
// scanning code, minus any need for error handling.
//
// !!! Error handling is still included due to running common routines, but
// should be factored out for efficiency.
//
#if !defined(CPLUSPLUS_11)
    //
    // Plain C build uses trivial expansion of REBCHR() and REBCHR(const*)
    //
    // REBCHR(*) cp; => REBYTE * cp;
    // REBCHR(const*) cp; => REBYTE const* cp;
    //
    #define REBCHR(star_or_const_star) \
        REBYTE star_or_const_star

    inline static REBYTE* NEXT_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp
    ){
        if (*bp < 0x80)
            *codepoint_out = *bp;
        else
            bp = Back_Scan_UTF8_Char(codepoint_out, bp, NULL);
        return m_cast(REBYTE*, bp + 1);
    }

    inline static REBYTE* BACK_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp
    ){
        NEXT_CHR(codepoint_out, bp);
        --bp;
        while ((*bp & 0xC0) == 0x80)
            --bp;
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* NEXT_STR(const REBYTE *bp) {
        do {
            ++bp;
        } while ((*bp & 0xC0) == 0x80);
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* BACK_STR(const REBYTE *bp) {
        do {
            --bp;
        } while ((*bp & 0xC0) == 0x80);
        return m_cast(REBYTE*, bp);
    }

    inline static REBYTE* SKIP_CHR(
        REBUNI *codepoint_out,
        const REBYTE *bp,
        REBINT delta
    ){
        REBINT n = delta;
        while (n != 0) {
            if (delta > 0) {
                bp = NEXT_CHR(codepoint_out, bp);
                --n;
            }
            else {
                bp = BACK_CHR(codepoint_out, bp);
                ++n;
            }
        }
        return m_cast(REBYTE*, bp);
    }


    inline static REBUNI CHR_CODE(const REBYTE *bp) {
        REBUNI codepoint;
        NEXT_CHR(&codepoint, bp);
        return codepoint;
    }

    inline static REBYTE* WRITE_CHR(REBYTE* bp, REBUNI codepoint) {
        return bp + Encode_UTF8_Char(bp, codepoint);
    }
#else
    // C++ build uses templates to expand REBCHR(*) and REBCHR(const*) into
    // pointer classes.  This technique allows the simple C compilation too:
    //
    // http://blog.hostilefork.com/kinda-smart-pointers-in-c/
    //
    // NOTE: Don't put this in %reb-defs.h and try to pass REBCHR(*) as args
    // to routines as part of the %sys-core.h API.  This would lead to an
    // incompatible runtime interface between C and C++ builds of cores and
    // extensions using the internal API--which we want to avoid!
    //
    template<typename T> struct RebchrPtr;
    #define REBCHR(star_or_const_star) \
        RebchrPtr<REBYTE star_or_const_star>

    // Primary purpose of the classes is to disable the ability to directly
    // increment or decrement pointers to REBYTE* without going through helper
    // routines that do decoding.

    template<>
    struct RebchrPtr<const REBYTE*> {
        const REBYTE *bp;

        RebchrPtr () {}
        explicit RebchrPtr (const REBYTE *bp) : bp (bp) {}

        RebchrPtr next(REBUNI *out) {
            const REBYTE *t = bp;
            if (*t < 0x80)
                *out = *t;
            else
                t = Back_Scan_UTF8_Char(out, t, NULL);
            return cast(REBCHR(const*), t + 1);
        }

        RebchrPtr back(REBUNI *out) {
            next(out);
            const REBYTE *t = bp;
            --t;
            while ((*t & 0xC0) == 0x80)
                --t;
            return cast(REBCHR(const*), t);
        }

        RebchrPtr next_only() {
            const REBYTE *t = bp;
            do {
                ++t;
            } while ((*t & 0xC0) == 0x80);
            return cast(REBCHR(const*), t);
        }

        RebchrPtr back_only() {
            const REBYTE *t = bp;
            do {
                --t;
            } while ((*t & 0xC0) == 0x80);
            return cast(REBCHR(const*), t);
        }

        RebchrPtr skip(REBUNI *out, REBINT delta) {
            assert(delta != 0);
            REBINT n = delta;
            const REBYTE *t = bp;  // shouldn't be used
            while (n != 0) {
                if (delta > 0) {
                    t = next(out).bp;
                    --n;
                }
                else {
                    t = back(out).bp;
                    ++n;
                }
            }
            return cast(REBCHR(const*), t);
        }

        REBUNI code() {
            REBUNI codepoint;
            next(&codepoint);
            return codepoint;
        }

        operator const void * () { return bp; }

        REBSIZ operator-(const REBYTE *rhs)
          { return bp - rhs; }

        REBSIZ operator-(RebchrPtr rhs)
          { return bp - rhs.bp; }

        bool operator==(const RebchrPtr<const REBYTE*> &other)
          { return bp == other.bp; }

        bool operator==(const REBYTE *other)
          { return bp == other; }

        bool operator!=(const RebchrPtr<const REBYTE*> &other)
          { return bp != other.bp; }

        bool operator!=(const REBYTE *other)
          { return bp != other; }

        operator const REBYTE*() { return bp; }  // implicit cast
    };

    template<>
    struct RebchrPtr<REBYTE*> : public RebchrPtr<const REBYTE*> {
        RebchrPtr () : RebchrPtr<const REBYTE*>() {}
        explicit RebchrPtr (REBYTE *bp) : RebchrPtr<const REBYTE*> (bp) {}

        static REBCHR(*) nonconst(REBCHR(const*) cp)
          { return cast(REBCHR(*), m_cast(REBYTE*, cp.bp)); }

        RebchrPtr back(REBUNI *out)
          { return nonconst(REBCHR(const*)::back(out)); }

        RebchrPtr next(REBUNI *out)
          { return nonconst(REBCHR(const*)::next(out)); }

        RebchrPtr back_only()
          { return nonconst(REBCHR(const*)::back_only()); }

        RebchrPtr next_only()
          { return nonconst(REBCHR(const*)::next_only()); }

        RebchrPtr skip(REBUNI *out, REBINT delta)
          { return nonconst(REBCHR(const*)::skip(out, delta)); }

        RebchrPtr write(REBUNI codepoint) {
            return cast(
                REBCHR(*),
                m_cast(REBYTE*, bp)
                    + Encode_UTF8_Char(m_cast(REBYTE*, bp), codepoint)
            );
        }

        operator void * () { return m_cast(REBYTE*, bp); }

        operator REBYTE*() { return m_cast(REBYTE*, bp); }  // implicit cast
    };

    #define NEXT_CHR(out, cp)               (cp).next(out)
    #define BACK_CHR(out, cp)               (cp).back(out)
    #define NEXT_STR(cp)                    (cp).next_only()
    #define BACK_STR(cp)                    (cp).back_only()
    #define SKIP_CHR(out,cp,delta)          (cp).skip((out), (delta))
    #define CHR_CODE(cp)                    (cp).code()
    #define WRITE_CHR(cp, codepoint)        (cp).write(codepoint)
#endif


//=//// SAFE COMPARISONS WITH BUILT-IN SYMBOLS ////////////////////////////=//
//
// R3-Alpha's concept was that all words got persistent integer values, which
// prevented garbage collection.  Ren-C only gives built-in words integer
// values--or SYMs--while others must be compared by pointers to their
// name or canon-name pointers.  A non-built-in symbol will return SYM_0 as
// its symbol, allowing it to fall through to defaults in case statements.
//
// Though it works fine for switch statements, it creates a problem if someone
// writes `VAL_WORD_SYM(a) == VAL_WORD_SYM(b)`, because all non-built-ins
// will appear to be equal.  It's a tricky enough bug to catch to warrant an
// extra check in C++ that disallows comparing SYMs with ==

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    //
    // Trivial definition for C build or release builds: symbols are just a C
    // enum value and an OPT_REBSYM acts just like a REBSYM.
    //
    typedef enum Reb_Symbol REBSYM;
    typedef enum Reb_Symbol OPT_REBSYM;
#else
    struct REBSYM;

    struct OPT_REBSYM {  // may only be converted to REBSYM, no comparisons
        enum Reb_Symbol n;
        OPT_REBSYM (const REBSYM& sym);
        bool operator==(enum Reb_Symbol other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol other) const
          { return n != other; }

        bool operator==(OPT_REBSYM &&other) const = delete;
        bool operator!=(OPT_REBSYM &&other) const = delete;

        operator unsigned int() const
          { return cast(unsigned int, n); }
    };

    struct REBSYM {  // acts like a REBOL_Symbol with no OPT_REBSYM compares
        enum Reb_Symbol n;
        REBSYM () {}
        REBSYM (int n) : n (cast(enum Reb_Symbol, n)) {}
        REBSYM (OPT_REBSYM opt_sym) : n (opt_sym.n) {}
        operator unsigned int() const
          { return cast(unsigned int, n); }

        bool operator>=(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n >= other;
        }
        bool operator<=(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n <= other;
        }
        bool operator>(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n > other;
        }
        bool operator<(enum Reb_Symbol other) const {
            assert(other != SYM_0);
            return n < other;
        }
        bool operator==(enum Reb_Symbol other) const
          { return n == other; }
        bool operator!=(enum Reb_Symbol other) const
          { return n != other; }

        bool operator==(REBSYM &other) const = delete;  // may be SYM_0
        void operator!=(REBSYM &other) const = delete;  // ...same
        bool operator==(const OPT_REBSYM &other) const = delete;  // ...same
        void operator!=(const OPT_REBSYM &other) const = delete;  // ...same
    };

    inline OPT_REBSYM::OPT_REBSYM(const REBSYM &sym) : n (sym.n) {}
#endif

inline static bool SAME_SYM_NONZERO(REBSYM a, REBSYM b) {
    assert(a != SYM_0 and b != SYM_0);
    return cast(REBCNT, a) == cast(REBCNT, b);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBSTR series for UTF-8 strings
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The concept is that a SYM refers to one of the built-in words and can
// be used in C switch statements.  A canon STR is used to identify
// everything else.
//

#define STR(p) \
    SER(p)  // !!! Enhance with more checks, like SER(), NOD(), etc.

inline static const char *STR_HEAD(REBSTR *str) {
    return cs_cast(BIN_HEAD(str));
}

inline static REBSTR *STR_CANON(REBSTR *str) {
    assert(SER_WIDE(str) == 1);
    while (NOT_SERIES_INFO(str, STRING_CANON))
        str = LINK(str).synonym; // circularly linked list
    return str;
}

inline static OPT_REBSYM STR_SYMBOL(REBSTR *str) {
    uint16_t sym = SECOND_UINT16(str->header);
    assert(sym == SECOND_UINT16(STR_CANON(str)->header));
    return cast(REBSYM, sym);
}

inline static size_t STR_SIZE(REBSTR *str) {
    return SER_LEN(str); // number of bytes in seris is series length, ATM
}

inline static REBSTR *Canon(REBSYM sym) {
    assert(cast(REBCNT, sym) != 0);
    assert(cast(REBCNT, sym) < SER_LEN(PG_Symbol_Canons));
    return *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym));
}

inline static bool SAME_STR(REBSTR *s1, REBSTR *s2) {
    if (s1 == s2)
        return true; // !!! does this check speed things up or not?
    return STR_CANON(s1) == STR_CANON(s2); // canon check, quite fast
}



//
// UNI_XXX: These are for dealing with the series behind an ANY-STRING!
// Currently they are slightly different than the STR_XXX functions, because
// the ANY-WORD! series don't store lengths or modification stamps.  (This
// makes sense because an interned word is immutable, so it wouldn't need
// a modification stamp.)
//

inline static REBCNT UNI_LEN(REBSER *s) {
    assert(SER_WIDE(s) == sizeof(REBYTE));
    assert(GET_SERIES_FLAG(s, UTF8_NONWORD));

  #if defined(DEBUG_UTF8_EVERYWHERE)
    if (MISC(s).length > SER_USED(s)) // includes 0xDECAFBAD
        panic(s);
  #endif
    return MISC(s).length;
}

inline static void SET_UNI_LEN_USED(REBSER *s, REBCNT len, REBSIZ used) {
    assert(SER_WIDE(s) == sizeof(REBYTE));
    assert(GET_SERIES_FLAG(s, UTF8_NONWORD));

    SET_SERIES_USED(s, used);
    MISC(s).length = len;
}

inline static void TERM_UNI_LEN_USED(REBSER *s, REBCNT len, REBSIZ used) {
    SET_UNI_LEN_USED(s, len, used);
    TERM_SEQUENCE(s);
}

#define UNI_HEAD(s) \
    cast(REBCHR(*), SER_HEAD(REBYTE, (s)))

#define UNI_TAIL(s) \
    cast(REBCHR(*), SER_TAIL(REBYTE, (s)))

#define UNI_LAST(s) \
    cast(REBCHR(*), SER_LAST(REBYTE, (s)))

inline static REBCHR(*) UNI_AT(REBSER *s, REBCNT n) {
    REBCHR(*) cp = UNI_HEAD(s);
    REBCNT i = n;
    for (; i != 0; --i)
        cp = NEXT_STR(cp); // !!! crazy slow
    return cp;
}

#define VAL_UNI_HEAD(v) \
    UNI_HEAD(VAL_SERIES(v))

#define VAL_UNI_TAIL(v) \
    UNI_TAIL(VAL_SERIES(v))

// This should be an updating operation, which may refresh the cache in the
// value.  It would look something like:
//
//     if (s->stamp == v->extra.utfcache.stamp)
//          return v->extra.utfcache.offset;
//     ...else calculate...
//    m_cast(REBVAL*, v)->extra.utfcache.stamp = s->stamp;
//    m_cast(REBVAL*, v)->extra.utfcache.offset = offset;
//
// One should thus always prefer to use VAL_UNI_AT() if possible, over trying
// to calculate a position from scratch.
//
inline static REBCHR(*) VAL_UNI_AT(const REBCEL *v) {
    assert(ANY_STRING_KIND(CELL_KIND(v)));
    return UNI_AT(VAL_SERIES(v), VAL_INDEX(v));
}

inline static REBSIZ VAL_SIZE_LIMIT_AT(
    REBCNT *length, // length in chars to end (including limit)
    const REBCEL *v,
    REBINT limit // -1 for no limit
){
    assert(ANY_STRING_KIND(CELL_KIND(v)));

    REBCHR(const *) at = VAL_UNI_AT(v); // !!! update cache if needed
    REBCHR(const *) tail;

    if (limit == -1) {
        if (length != NULL)
            *length = VAL_LEN_AT(v);
        tail = VAL_UNI_TAIL(v); // byte count known (fast)
    }
    else {
        if (length != NULL)
            *length = limit;
        tail = at;
        for (; limit > 0; --limit)
            tail = NEXT_STR(tail);
    }

    return tail - at;
}

#define VAL_SIZE_AT(v) \
    VAL_SIZE_LIMIT_AT(NULL, v, -1)

inline static REBSIZ VAL_OFFSET(const RELVAL *v) {
    return VAL_UNI_AT(v) - VAL_UNI_HEAD(v);
}

inline static REBSIZ VAL_OFFSET_FOR_INDEX(const REBCEL *v, REBCNT index) {
    assert(ANY_STRING_KIND(CELL_KIND(v)));

    REBCHR(const *) at;

    if (index == VAL_INDEX(v))
        at = VAL_UNI_AT(v); // !!! update cache if needed
    else if (index == VAL_LEN_HEAD(v))
        at = VAL_UNI_TAIL(v);
    else {
        // !!! arbitrary seeking...this technique needs to be tuned, e.g.
        // to look from the head or the tail depending on what's closer
        //
        at = UNI_AT(VAL_SERIES(v), index);
    }

    return at - VAL_UNI_HEAD(v);
}


//
// Get or set a unit in a binary series or a string series.  Used by routines
// that do searching/etc. and want to apply to both BINARY! and ANY-STRING!,
// so it can't be converted to purely UTF-8 as written.
//
// !!! String logic will get more complex with UTF8-Everywhere; it may have to
// shift bytes out of the way.  Or it may not even be possible to set a
// character if there aren't characters established before it.  Any
// algorithm using these should likely instead be using the mold buffer to
// create new strings, if possible.
//

inline static REBUNI GET_ANY_CHAR(REBSER *s, REBCNT n) {
    if (GET_SERIES_FLAG(s, UTF8_NONWORD)) {
        REBCHR(const *) up = UNI_AT(s, n);
        REBUNI c;
        NEXT_CHR(&c, up);
        return c;
    }
    return *BIN_AT(s, n);
}

inline static void SET_ANY_CHAR(REBSER *s, REBCNT n, REBUNI c) {
    assert(n < SER_LEN(s));

    if (GET_SERIES_FLAG(s, UTF8_NONWORD)) {
        REBCHR(*) up = UNI_AT(s, n);
        WRITE_CHR(up, c);
    }
    else {
        assert(c <= 255);
        *BIN_AT(s, n) = c;
    }
}

#define VAL_ANY_CHAR(v) \
    GET_ANY_CHAR(VAL_SERIES(v), VAL_INDEX(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-STRING! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Init_Text(v,s) \
    Init_Any_Series((v), REB_TEXT, (s))

#define Init_File(v,s) \
    Init_Any_Series((v), REB_FILE, (s))

#define Init_Email(v,s) \
    Init_Any_Series((v), REB_EMAIL, (s))

#define Init_Tag(v,s) \
    Init_Any_Series((v), REB_TAG, (s))

#define Init_Url(v,s) \
    Init_Any_Series((v), REB_URL, (s))


// Basic string initialization from UTF8.  (Most clients should be using the
// rebStringXXX() APIs for this).  Note that these routines may fail() if the
// data they are given is not UTF-8.

inline static REBSER *Make_String_UTF8(const char *utf8)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(NULL, utf8, strsize(utf8), crlf_to_lf);
}

inline static REBSER *Make_Sized_String_UTF8(const char *utf8, size_t size)
{
    const bool crlf_to_lf = false;
    return Append_UTF8_May_Fail(NULL, utf8, size, crlf_to_lf);
}


inline static REBINT Hash_String(REBSTR *str)
    { return Hash_UTF8(cb_cast(STR_HEAD(str)), STR_SIZE(str)); }

inline static REBINT First_Hash_Candidate_Slot(
    REBCNT *skip_out,
    REBCNT hash,
    REBCNT num_slots
){
    *skip_out = (hash & 0x0000FFFF) % num_slots;
    if (*skip_out == 0)
        *skip_out = 1;
    return (hash & 0x00FFFF00) % num_slots;
}


//
// Copy helpers
//

inline static REBSER *Copy_String_At(const RELVAL *v)
{
    return Copy_String_At_Limit(v, -1);
}

inline static REBSER *Copy_Sequence_At_Len(
    REBSER *s,
    REBCNT index,
    REBCNT len
){
    return Copy_Sequence_At_Len_Extra(s, index, len, 0);
}


// This is a speculative routine, which is based on the idea that it will be
// common for UTF-8 anywhere strings to cache a bit saying whether they are
// in ASCII range and fixed size.  If this is the case, different algorithms
// might be applied, for instance a standard C qsort() to sort the characters.
//
inline static bool Is_String_Definitely_ASCII(const RELVAL *str) {
    UNUSED(str);
    return false;
}

#define Make_String(encoded_capacity) \
    Make_String_Core((encoded_capacity), SERIES_FLAGS_NONE)
