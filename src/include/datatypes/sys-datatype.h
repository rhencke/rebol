//
//  File: %sys-datatype.h
//  Summary: "DATATYPE! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Rebol Open Source Contributors
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
// Note: R3-Alpha's notion of a datatype has not been revisited very much in
// Ren-C.  The unimplemented UTYPE! user-defined type concept was removed
// for simplification, pending a broader review of what was needed.
//
// %words.r is arranged so symbols for types are at the start of the enum.
// Note REB_0 is not a type, which lines up with SYM_0 used for symbol IDs as
// "no symbol".  Also, NULL is not a value type, and is at REB_MAX past the
// end of the list.
//
// !!! Consider renaming (or adding a synonym) to just TYPE!
//

#define VAL_TYPE_KIND(v) \
    EXTRA(Datatype, (v)).kind

#define VAL_TYPE_SPEC_NODE(v) \
    PAYLOAD(Any, (v)).first.node

#define VAL_TYPE_SPEC(v) \
    ARR(VAL_TYPE_SPEC_NODE(v))
