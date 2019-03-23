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
// The CHAR! datatype stores both a codepoint and the bytes of the character
// encoded.  It's relatively inexpensive to do the encoding, and almost
// always necessary to have it available.
//
// Historically there is some disagremeent on UTF-8 codepoint maximum size:
//
//     "UTF-8 was originally specified to allow codepoints with up to
//     31 bits (or 6 bytes). But with RFC3629, this was reduced to 4
//     bytes max. to be more compatible to UTF-16."  So depending on
//     which RFC you consider "the UTF-8", max size is either 4 or 6.
//
// Rebol generally assumes 4, which goes with the general consensus:
//
// https://stackoverflow.com/a/9533324/211160
//
// The encoded payload takes the whole 8 bytes of a 32-bit payload.  The first
// is used for the encoded length, then the encoding, then a null terminator.
// This should leave two bytes for something else if it were needed.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The 0 codepoint ("NUL") is a valid CHAR! -but- it can not appear in an
//   ANY-STRING!.  Only BINARY! can have embedded zero bytes.  For strings it
//   is kept for termination, so that only one return result is needed from
//   APIs like rebSpell().  All efforts are being made to make it as easy to
//   work with a BINARY! on string-like tasks where internal 0 bytes are ok.
//

#if !defined(__cplusplus)
    #define VAL_CHAR(v) \
        EXTRA(Character, (v)).codepoint
#else
    inline static REBUNI const & VAL_CHAR(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_CHAR);
        return EXTRA(Character, (v)).codepoint;
    }

    inline static REBUNI & VAL_CHAR(REBCEL *v) {
        assert(CELL_KIND(v) == REB_CHAR);
        return EXTRA(Character, (v)).codepoint;
    }
#endif

inline static REBYTE VAL_CHAR_ENCODED_SIZE(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_CHAR);
    assert(PAYLOAD(Character, (v)).size_then_encoded[0] <= 4);
    return PAYLOAD(Character, (v)).size_then_encoded[0];
}

inline static const REBYTE *VAL_CHAR_ENCODED(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_CHAR);
    return &(PAYLOAD(Character, (v)).size_then_encoded[1]);  // [0] is size
}


extern const uint_fast8_t firstByteMark[7];  // defined in %s-unicode.c

inline static uint_fast8_t Encoded_Size_For_Codepoint(REBUNI c) {
    if (c < cast(uint32_t, 0x80))
        return 1;
    if (c < cast(uint32_t, 0x800))
        return 2;
    if (c < cast(uint32_t, 0x10000))
        return 3;
   if (c <= UNI_MAX_LEGAL_UTF32)
        return 4;

    /*len = 3;
    c = UNI_REPLACEMENT_CHAR; */  // previous code could tolerate

    fail ("Codepoint is greater than maximum legal UTF-32 value");        
}

// Converts a single char to UTF8 code-point.
// Returns length of char stored in dst.
// Be sure dst has at least 4 bytes available.
//
inline static uint_fast8_t Encode_UTF8_Char(REBYTE *dst, REBUNI c) {
    const uint32_t mask = 0xBF;
    const uint32_t mark = 0x80;

    uint_fast8_t len = Encoded_Size_For_Codepoint(c);
    dst += len;

    switch (len) {
      case 4:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6;  // falls through
      case 3:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6;  // falls through
      case 2:
        *--dst = cast(REBYTE, (c | mark) & mask);
        c >>= 6;  // falls through
      case 1:
        *--dst = cast(REBYTE, c | firstByteMark[len]);
    }

    return len;
}

// If you know that a codepoint is good (e.g. it came from an ANY-STRING!)
// this routine can be used.
//
inline static REBVAL *Init_Char_Unchecked(RELVAL *out, REBUNI uni) {
    RESET_CELL(out, REB_CHAR, CELL_MASK_NONE);
    VAL_CHAR(out) = uni;

    REBYTE len = Encode_UTF8_Char(
        &PAYLOAD(Character, out).size_then_encoded[1],
        uni
    );
    PAYLOAD(Character, out).size_then_encoded[0] = len;
    PAYLOAD(Character, out).size_then_encoded[len + 1] = '\0';
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Char_May_Fail(RELVAL *out, REBUNI uni) {
    if (uni > MAX_UNI) {
        DECLARE_LOCAL (temp);
        fail (Error_Codepoint_Too_High_Raw(Init_Integer(temp, uni)));
    }

    // !!! Should other values that can't be read be forbidden?  Byte order
    // mark?  UTF-16 surrogate stuff?  If something is not legitimate in a
    // UTF-8 codepoint stream, it shouldn't be used.

    return Init_Char_Unchecked(out, uni);
}

#define SPACE_VALUE \
    Root_Space_Char

#define NEWLINE_VALUE \
    Root_Newline_Char


extern const char trailingBytesForUTF8[256];  // defined in %s-unicode.c
extern const uint_fast32_t offsetsFromUTF8[6];  // defined in %s-unicode.c


// Converts a single UTF8 code-point and returns the position *at the
// the last byte of the character's data*.  (This differs from the usual
// `Scan_XXX` interface of returning the position after the scanned
// element, ready to read the next one.)
//
// The peculiar interface is useful in loops that are processing
// ordinary ASCII chars directly -as well- as UTF8 ones.  The loop can
// do a single byte pointer increment after both kinds of
// elements, avoiding the need to call any kind of `Scan_Ascii()`:
//
//     for (; size > 0; ++bp, --size) {
//         if (*bp < 0x80) {
//             // do ASCII stuff...
//         }
//         else {
//             REBUNI uni;
//             bp = Back_Scan_UTF8_Char(&uni, bp, &size);
//             // do UNICODE stuff...
//         }
//     }
//
// The third parameter is an optional size that will be decremented by
// the number of "extra" bytes the UTF8 has beyond a single byte character.
// This allows for decrement-style loops such as the above.
//
// Prescans source for null, and will not return code point 0.
//
// If failure due to insufficient data or malformed bytes, then NULL is
// returned (size is not advanced).
//
inline static const REBYTE *Back_Scan_UTF8_Char(
    REBUNI *out, // "UTF32" is defined as unsigned long above
    const REBYTE *bp,
    REBSIZ *size
){
    *out = 0;

    const REBYTE *source = bp;
    uint_fast8_t trail = trailingBytesForUTF8[*source];

    // Check that we have enough valid source bytes:
    if (size) {
        if (cast(uint_fast8_t, trail + 1) > *size)
            return nullptr;
    }
    else if (trail != 0) {
        do {
            if (source[trail] < 0x80)
                return nullptr;
        } while (--trail != 0);

        trail = trailingBytesForUTF8[*source];
    }

    // Do this check whether lenient or strict:
    // if (!isLegalUTF8(source, slen+1)) return 0;

    switch (trail) {
        case 5: *out += *source++; *out <<= 6;  // falls through
        case 4: *out += *source++; *out <<= 6;  // falls through
        case 3: *out += *source++; *out <<= 6;  // falls through
        case 2: *out += *source++; *out <<= 6;  // falls through
        case 1: *out += *source++; *out <<= 6;  // falls through
        case 0: *out += *source++;
    }
    *out -= offsetsFromUTF8[trail];

    // UTF-16 surrogate values are illegal in UTF-32, and anything
    // over Plane 17 (> 0x10FFFF) is illegal.
    //
    if (*out > UNI_MAX_LEGAL_UTF32)
        return nullptr;
    if (*out >= UNI_SUR_HIGH_START && *out <= UNI_SUR_LOW_END)
        return nullptr;

    if (size)
        *size -= trail;

    // !!! Original implementation used 0 as a return value to indicate a
    // decoding failure.  However, 0 is a legal UTF8 codepoint, and also
    // Rebol strings are able to store NUL characters (they track a length
    // and are not zero-terminated.)  Should this be legal?
    //
    assert(*out != 0);
    if (*out == 0)
        return nullptr;

    return bp + trail;
}
