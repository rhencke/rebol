//
//  File: %sys-bitset.h
//  Summary: "BITSET! Datatype Header"
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
//

inline static REBBIN *VAL_BITSET(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_BITSET);
    return SER(VAL_NODE(v));
}

inline static REBVAL *Init_Bitset(RELVAL *out, REBBIN *bits) {
    RESET_CELL(out, REB_BITSET, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(out, bits);
    return KNOWN(out);
}
