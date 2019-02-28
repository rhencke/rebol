//
//  File: %sys-action.h
//  Summary: "action! defs BEFORE %tmp-internals.h (see: %sys-action.h)"
//  Section: core
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
//=////////////////////////////////////////////////////////////////////////=//
//


struct Reb_Action {
    struct Reb_Array paramlist;
};


// Includes SERIES_FLAG_ALWAYS_DYNAMIC because an action's paramlist is always
// allocated dynamically, in order to make access to the archetype and the
// parameters faster than ARR_AT().  See code for ACT_PARAM(), etc.
//
// Includes SERIES_FLAG_FIXED_SIZE because for now, the user can't expand
// them (e.g. by APPENDing to a FRAME! value).  Also, no internal tricks
// for function composition expand them either at this time.
//
#define SERIES_MASK_ACTION \
    (NODE_FLAG_NODE | SERIES_FLAG_ALWAYS_DYNAMIC | SERIES_FLAG_FIXED_SIZE \
        | ARRAY_FLAG_IS_PARAMLIST)


#if !defined(DEBUG_CHECK_CASTS) || !defined(CPLUSPLUS_11)

    #define ACT(p) \
        cast(REBACT*, (p))

#else

    template <typename P>
    inline REBACT *ACT(P p) {
        constexpr bool derived =
            std::is_same<P, nullptr_t>::value  // here to avoid check below
            or std::is_same<P, REBACT*>::value;

        constexpr bool base =
            std::is_same<P, void*>::value
            or std::is_same<P, REBNOD*>::value
            or std::is_same<P, REBSER*>::value
            or std::is_same<P, REBARR*>::value;

        static_assert(
            derived or base,
            "ACT() works on void/REBNOD/REBSER/REBARR/REBACT/nullptr"
        );

        if (base and p)  // ACT(nullptr) won't be tested here
            assert(
                SERIES_MASK_ACTION == (cast(REBSER*, p)->header.bits & (
                    SERIES_MASK_ACTION
                        | NODE_FLAG_FREE
                        | NODE_FLAG_CELL
                        | ARRAY_FLAG_IS_VARLIST
                        | ARRAY_FLAG_IS_PAIRLIST
                ))
            );

        // !!! This uses a regular C cast because the `cast()` macro has not
        // been written in such a way as to tolerate nullptr, and C++ will
        // not reinterpret_cast<> a nullptr.  Review more elegant answers.
        //
        return (REBACT*)p;
    }

#endif
