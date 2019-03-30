//
//  File: %sys-binary.h
//  Summary: {Definitions for binary series}
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
//=////////////////////////////////////////////////////////////////////////=//
//


// Is it a byte-sized series?
//
#define BYTE_SIZE(s) \
    (SER_WIDE(s) == 1)


//
// BIN_XXX: Binary or byte-size string seres macros
//

#define BIN_AT(s,n) \
    SER_AT(REBYTE, (s), (n))

#define BIN_HEAD(s) \
    SER_HEAD(REBYTE, (s))

#define BIN_TAIL(s) \
    SER_TAIL(REBYTE, (s))

#define BIN_LAST(s) \
    SER_LAST(REBYTE, (s))

inline static REBCNT BIN_LEN(REBBIN *s) {
    assert(BYTE_SIZE(s));
    return SER_USED(s);
}

inline static void TERM_BIN(REBSER *s) {
    BIN_HEAD(s)[SER_USED(s)] = 0;
}

inline static void TERM_BIN_LEN(REBSER *s, REBCNT len) {
    SET_SERIES_USED(s, len);
    BIN_HEAD(s)[len] = 0;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_BIN_HEAD(v) \
    BIN_HEAD(VAL_SERIES(v))

inline static REBYTE *VAL_BIN_AT(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_BINARY or CELL_KIND(v) == REB_BITSET);
    if (VAL_PAST_END(v))
        fail (Error_Past_End_Raw());  // don't give deceptive return pointer
    return BIN_AT(VAL_SERIES(v), VAL_INDEX(v));
}

// !!! RE: VAL_BIN_AT_HEAD() see remarks on VAL_ARRAY_AT_HEAD()
//
#define VAL_BIN_AT_HEAD(v,n) \
    BIN_AT(VAL_SERIES(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(VAL_SERIES(v))

// defined as an inline to avoid side effects in:

#define Init_Binary(out, bin) \
    Init_Any_Series((out), REB_BINARY, (bin))

inline static REBBIN *VAL_BINARY(const REBCEL* v) {
    assert(CELL_KIND(v) == REB_BINARY);
    return VAL_SERIES(v);
}


// Make a byte series of length 0 with the given capacity.  The length will
// be increased by one in order to allow for a null terminator.  Binaries are
// given enough capacity to have a null terminator in case they are aliased
// as UTF-8 data later, e.g. `as word! binary`, since it would be too late
// to give them that capacity after-the-fact to enable this.
//
inline static REBSER *Make_Binary_Core(REBCNT capacity, REBFLGS flags)
{
    REBSER *bin = Make_Series_Core(capacity + 1, sizeof(REBYTE), flags);
    TERM_SEQUENCE(bin);
    return bin;
}

#define Make_Binary(capacity) \
    Make_Binary_Core(capacity, SERIES_FLAGS_NONE)
