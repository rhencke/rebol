//
//  File: %sys-char.h
//  Summary: "CHAR! Datatype Header"
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

#define MAX_CHAR 0xffff

#define VAL_CHAR(v) \
    PAYLOAD(Character, (v)).codepoint

inline static REBVAL *Init_Char(RELVAL *out, REBUNI uni) {
    RESET_CELL(out, REB_CHAR, CELL_MASK_NONE);
    VAL_CHAR(out) = uni;
    return cast(REBVAL*, out);
}

#define SPACE_VALUE \
    Root_Space_Char

#define NEWLINE_VALUE \
    Root_Newline_Char
