//
//  File: %sys-vector.c
//  Summary: "Vector Datatype header file"
//  Section: datatypes
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
// The cell for a REB_VECTOR points to a "pairing"--which is two value cells
// stored in an optimized format that fits inside one REBSER node.  This is
// a relatively light allocation, which allows the vector's properties
// (bit width, signedness, integral-ness) to be stored in addition to a
// BINARY! of the vector's bytes.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * See %src/extensions/vector/README.md
//

#define VAL_VECTOR_BINARY(v) \
    PAYLOAD(Vector, (v)).paired  // pairing[0]

#define VAL_VECTOR_SIGN_INTEGRAL_WIDE(v) \
    PAIRING_KEY(PAYLOAD(Vector, (v)).paired)  // pairing[1]

#define VAL_VECTOR_SIGN(v) \
    PAYLOAD(Custom, VAL_VECTOR_SIGN_INTEGRAL_WIDE(v)).first.b

inline static bool VAL_VECTOR_INTEGRAL(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_VECTOR);
    REBVAL *sib = VAL_VECTOR_SIGN_INTEGRAL_WIDE(v);
    if (PAYLOAD(Custom, sib).second.b != 0)
        return true;

    assert(VAL_VECTOR_SIGN(v));
    return false;
}

inline static REBYTE VAL_VECTOR_WIDE(const REBCEL *v) {  // "wide" REBSER term
    int32_t wide = EXTRA(Custom, VAL_VECTOR_SIGN_INTEGRAL_WIDE(v)).i32;
    assert(wide == 1 or wide == 2 or wide == 3 or wide == 4);
    return wide;
}

#define VAL_VECTOR_BITSIZE(v) \
    (VAL_VECTOR_WIDE(v) * 8)

inline static REBYTE *VAL_VECTOR_HEAD(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_VECTOR);
    return VAL_BIN_HEAD(PAYLOAD(Vector, v).paired);
}

inline static REBCNT VAL_VECTOR_LEN_AT(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_VECTOR);
    return VAL_LEN_HEAD(VAL_VECTOR_BINARY(v)) / VAL_VECTOR_WIDE(v);
}

#define VAL_VECTOR_INDEX(v) 0  // !!! Index not currently supported
#define VAL_VECTOR_LEN_HEAD(v) VAL_VECTOR_LEN_AT(v)

inline static REBVAL *Init_Vector(
    RELVAL *out,
    REBBIN *bin,
    bool integral,
    bool sign,
    REBYTE bitsize
){
    RESET_CELL(out, REB_VECTOR);

    REBVAL *paired = Alloc_Pairing();

    Init_Binary(paired, bin);
    assert(SER_LEN(bin) % (bitsize / 8) == 0);

    REBVAL *sib = RESET_CELL(PAIRING_KEY(paired), REB_V_SIGN_INTEGRAL_BITS);
    assert(bitsize == 8 or bitsize == 16 or bitsize == 32 or bitsize == 64);
    EXTRA(Custom, sib).i32 = bitsize / 8;  // e.g. VAL_VECTOR_WIDE()
    PAYLOAD(Custom, sib).first.b = sign;
    PAYLOAD(Custom, sib).second.b = integral;

    Manage_Pairing(paired);
    PAYLOAD(Vector, out).paired = paired;
    return KNOWN(out);
}


// !!! These hooks allow the REB_VECTOR cell type to dispatch to code in the
// VECTOR! extension if it is loaded.
//
extern REBINT CT_Vector(const REBCEL *a, const REBCEL *b, REBINT mode);
extern REB_R MAKE_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern REB_R TO_Vector(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg);
extern void MF_Vector(REB_MOLD *mo, const REBCEL *v, bool form);
extern REBTYPE(Vector);
extern REB_R PD_Vector(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval);
