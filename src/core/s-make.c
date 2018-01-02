//
//  File: %s-make.c
//  Summary: "binary and unicode string support"
//  Section: strings
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

#include "sys-core.h"


//
//  Make_Binary: C
//
// Make a byte series of length 0 with the given capacity.  The length will
// be increased by one in order to allow for a null terminator.  Binaries are
// given enough capacity to have a null terminator in case they are aliased
// as UTF-8 data later, e.g. `as word! binary`, since it would be too late
// to give them that capacity after-the-fact to enable this.
//
REBSER *Make_Binary(REBCNT capacity)
{
    REBSER *bin = Make_Series(capacity + 1, sizeof(REBYTE));
    TERM_SEQUENCE(bin);
    return bin;
}


//
//  Make_String: C
//
// Makes a series to hold a string with enough capacity for a certain amount
// of encoded data.  Make_Unicode() is how more conservative guesses might
// be made, but better to either know -or- go via the mold buffer to get
// the exact right length.
//
REBSER *Make_String(REBSIZ encoded_capacity)
{
    // !!! Even though this is a byte sized sequence, we add 2 bytes for the
    // terminator (and TERM_SEQUENCE() terminates with 2 bytes) because we're
    // in a stopgap position where the series contains REBUNIs, and sometimes
    // the null terminator is visited in enumerations.
    //
    REBSER *s = Make_Series(encoded_capacity + 2, sizeof(REBYTE));
    SET_SERIES_FLAG(s, UCS2_STRING);
    MISC(s).length = 0;
    TERM_SERIES(s);

    // !!! Can the current codebase get away with single-byte termination of
    // a REBUNI-based string?  Code in the REBUNI-world shouldn't really be
    // looking for terminators since embedded nulls are legal in STRING!, so
    // we might get away with it.
    //
    ASSERT_SERIES_TERM(s);
    return s;
}


//
//  Make_Unicode: C
//
// !!! This is a very conservative string generator for UTF-8 content which
// assumes that you could need as much as 4 bytes per codepoint.  It's not a
// new issue, considering that R3-Alpha had to come up with a string size
// not knowing whether the string would need 1 or 2 byte codepoints.  But
// for internal strings, getting this wrong and not doing an expansion could
// be a bug.  Just make big strings for now.
//
REBSER *Make_Unicode(REBCNT codepoint_capacity)
{
    REBSER *s = Make_String(codepoint_capacity * 2);
    ASSERT_SERIES_TERM(s);
    return s;
}


//
//  Copy_Bytes: C
//
// Create a string series from the given bytes.
// Source is always latin-1 valid. Result is always 8bit.
//
REBSER *Copy_Bytes(const REBYTE *src, REBINT len)
{
    if (len < 0)
        len = LEN_BYTES(src);

    REBSER *dst = Make_Binary(len);
    memcpy(BIN_HEAD(dst), src, len);
    TERM_SEQUENCE_LEN(dst, len);

    return dst;
}


//
//  Insert_Char: C
//
// Insert a unicode char into a string.
//
void Insert_Char(REBSER *dst, REBCNT index, REBCNT chr)
{
    if (index > SER_LEN(dst))
        index = SER_LEN(dst);
    Expand_Series(dst, index, 1);
    SET_ANY_CHAR(dst, index, chr);
}


//
//  Copy_String_At_Limit: C
//
// !!! With UTF-8 Everywhere, copying strings will still be distinct from
// other series due to the length being counted in characters and not
// units of the series width.
//
REBSER *Copy_String_At_Limit(const RELVAL *src, REBINT limit)
{
    REBCNT length_limit;
    REBSIZ size = VAL_SIZE_LIMIT_AT(&length_limit, src, limit);
    assert(length_limit * 2 == size); // !!! Temporary

    REBSER *dst = Make_Unicode(size / 2);
    memcpy(AS_REBUNI(UNI_AT(dst, 0)), VAL_UNI_AT(src), size);
    TERM_SEQUENCE_LEN(dst, length_limit);

    return dst;
}


//
//  Append_Unencoded_Len: C
//
// Append unencoded data to a byte string, using plain memcpy().  If dst is
// NULL, a new byte-sized series will be created and returned.
//
REBSER *Append_Unencoded_Len(REBSER *dst, const char *src, REBCNT len)
{
    assert(BYTE_SIZE(dst));

    REBCNT tail;
    if (dst == NULL) {
        dst = Make_Binary(len);
        tail = 0;
    }
    else {
        tail = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, len);
    }

    memcpy(BIN_AT(dst, tail), src, len);
    TERM_SEQUENCE(dst);
    return dst;
}


//
//  Append_Unencoded: C
//
// Append_Unencoded_Len() variant that looks for a terminating 0 byte to
// determine the length.
//
// !!! Should be in a header file so it can be inlined.
//
REBSER *Append_Unencoded(REBSER *dst, const char *src)
{
    return Append_Unencoded_Len(dst, src, strlen(src));
}


//
//  Append_Codepoint: C
//
// Append a non-encoded character to a string.
//
REBSER *Append_Codepoint(REBSER *dst, REBUNI codepoint)
{
    REBCNT tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, sizeof(REBUNI));

    REBCHR(*) cp = UNI_AT(dst, tail);
    cp = WRITE_CHR(cp, codepoint);
    cp = WRITE_CHR(cp, '\0'); // should always be capacity for terminator

    return dst;
}


//
//  Append_Utf8_Codepoint: C
//
// Encode a codepoint onto a UTF-8 binary series.
//
REBSER *Append_Utf8_Codepoint(REBSER *dst, uint32_t codepoint)
{
    assert(SER_WIDE(dst) == sizeof(REBYTE));

    REBCNT tail = SER_LEN(dst);
    EXPAND_SERIES_TAIL(dst, 4); // !!! Conservative, assume long codepoint
    tail += Encode_UTF8_Char(BIN_AT(dst, tail), codepoint); // 1 to 4 bytes
    TERM_BIN_LEN(dst, tail);
    return dst;
}


//
//  Make_Ser_Codepoint: C
//
// Create a string that holds a single codepoint.
//
REBSER *Make_Ser_Codepoint(REBCNT codepoint)
{
    assert(codepoint < (1 << 16));

    REBSER *out = Make_Unicode(1);
    *UNI_HEAD(out) = codepoint;
    TERM_UNI_LEN(out, 1);

    return out;
}


//
//  Append_Utf8_Utf8: C
//
// Append a UTF8 byte series to a UTF8 binary.  Terminates.
//
// !!! Currently does the same thing as Append_Unencoded_Len.  Should it
// check the bytes to make sure they're actually UTF8?
//
void Append_Utf8_Utf8(REBSER *dst, const char *utf8, size_t size)
{
    Append_Unencoded_Len(dst, utf8, size);
}


//
//  Append_Spelling: C
//
// Append the spelling of a REBSTR to a UTF8 binary.  Terminates.
//
void Append_Spelling(REBSER *dst, REBSTR *spelling)
{
    Append_Utf8_Utf8(dst, STR_HEAD(spelling), STR_SIZE(spelling));
}


//
//  Append_Utf8_String: C
//
// Append a partial string to a UTF-8 binary series.
//
// !!! Used only with mold series at the moment.
//
void Append_Utf8_String(REBSER *dst, const RELVAL *src, REBCNT length_limit)
{
    assert(
        SER_WIDE(dst) == sizeof(REBYTE)
        and SER_WIDE(VAL_SERIES(src)) == sizeof(REBYTE)
        and GET_SERIES_FLAG(VAL_SERIES(src), UCS2_STRING)
    );

    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, src, length_limit);

    REBCNT tail = SER_LEN(dst);
    Expand_Series(dst, tail, size); // tail changed too

    memcpy(BIN_AT(dst, tail), BIN_AT(temp, offset), size);
}


//
//  Append_Int: C
//
// Append an integer string.
//
void Append_Int(REBSER *dst, REBINT num)
{
    REBYTE buf[32];

    Form_Int(buf, num);
    Append_Unencoded(dst, s_cast(buf));
}


//
//  Append_Int_Pad: C
//
// Append an integer string.
//
void Append_Int_Pad(REBSER *dst, REBINT num, REBINT digs)
{
    REBYTE buf[32];
    if (digs > 0)
        Form_Int_Pad(buf, num, digs, -digs, '0');
    else
        Form_Int_Pad(buf, num, -digs, digs, '0');

    Append_Unencoded(dst, s_cast(buf));
}



//
//  Append_UTF8_May_Fail: C
//
// Append UTF-8 data to a series underlying an ANY-STRING!.
//
// `dst = NULL` means make a new string.
//
REBSER *Append_UTF8_May_Fail(
    REBSER *dst,
    const char *utf8,
    size_t size,
    bool crlf_to_lf
){
    // This routine does not just append bytes blindly because:
    //
    // * We want to check for invalid codepoints (this can be called with
    //   arbitrary outside data from the API.
    // * It's needed to know how many characters (length) are in the series,
    //   not just how many bytes.  The higher level concept of "length" gets
    //   stored in the series MISC() field.
    // * In the future, some operations will be accelerated by knowing that
    //   a string only contains ASCII codepoints.

    assert(IS_SER_DYNAMIC(BUF_UTF8));
    BUF_UTF8->content.dynamic.used = 0;
    EXPAND_SERIES_TAIL(BUF_UTF8, size * 2); // at most this many unicode chars
    SET_SERIES_LEN(BUF_UTF8, 0);
    TERM_SERIES(BUF_UTF8);

    REBSER *temp = BUF_UTF8; // buffer is Unicode width

    REBUNI *up = UNI_HEAD(temp);
    const REBYTE *src = cb_cast(utf8);

    bool all_ascii = true;

    REBCNT num_codepoints = 0;

    REBSIZ bytes_left = size; // see remarks on Back_Scan_UTF8_Char's 3rd arg
    for (; bytes_left > 0; --bytes_left, ++src) {
        REBUNI ch = *src;
        if (ch >= 0x80) {
            src = Back_Scan_UTF8_Char(&ch, src, &bytes_left);
            if (src == NULL)
                fail (Error_Bad_Utf8_Raw());

            all_ascii = false;
        }
        else if (ch == CR && crlf_to_lf) {
            if (src[1] == LF)
                continue; // skip the CR, do the decrement and get the LF
            ch = LF;
        }

        ++num_codepoints;
        *up++ = ch;
    }

    UNUSED(all_ascii);

    up = UNI_HEAD(temp);

    REBCNT old_len;
    if (dst == NULL) {
        dst = Make_Unicode(num_codepoints);
        old_len = 0;
    }
    else {
        old_len = SER_LEN(dst);
        EXPAND_SERIES_TAIL(dst, num_codepoints);
    }

    REBUNI *dp = AS_REBUNI(UNI_AT(dst, old_len));
    SET_SERIES_LEN(dst, old_len + num_codepoints); // counted down to 0 below

    for (; num_codepoints > 0; --num_codepoints)
        *dp++ = *up++;

    TERM_SERIES(dst);
    return dst;
}


//
//  Join_Binary: C
//
// Join a binary from component values for use in standard
// actions like make, insert, or append.
// limit: maximum number of values to process
// limit < 0 means no limit
//
// WARNING: returns BYTE_BUF, not a copy!
//
REBSER *Join_Binary(const REBVAL *blk, REBINT limit)
{
    REBSER *series = BYTE_BUF;

    REBCNT tail = 0;

    if (limit < 0)
        limit = VAL_LEN_AT(blk);

    SET_SERIES_LEN(series, 0);

    RELVAL *val;
    for (val = VAL_ARRAY_AT(blk); limit > 0; val++, limit--) {
        switch (VAL_TYPE(val)) {
        case REB_INTEGER:
            if (VAL_INT64(val) > 255 || VAL_INT64(val) < 0)
                fail (Error_Out_Of_Range(KNOWN(val)));
            EXPAND_SERIES_TAIL(series, 1);
            *BIN_AT(series, tail) = (REBYTE)VAL_INT32(val);
            break;

        case REB_BINARY: {
            REBCNT len = VAL_LEN_AT(val);
            EXPAND_SERIES_TAIL(series, len);
            memcpy(BIN_AT(series, tail), VAL_BIN_AT(val), len);
            break; }

        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG: {
            REBCNT val_len = VAL_LEN_AT(val);
            size_t val_size = Size_As_UTF8(VAL_UNI_AT(val), val_len);

            EXPAND_SERIES_TAIL(series, val_size);
            SET_SERIES_LEN(
                series,
                tail + Encode_UTF8(
                    BIN_AT(series, tail),
                    val_size,
                    VAL_UNI_AT(val),
                    &val_len
                )
            );
            break; }

        case REB_CHAR: {
            EXPAND_SERIES_TAIL(series, 6);
            REBCNT len =
                Encode_UTF8_Char(BIN_AT(series, tail), VAL_CHAR(val));
            SET_SERIES_LEN(series, tail + len);
            break; }

        default:
            fail (Error_Bad_Value_Core(val, VAL_SPECIFIER(blk)));
        }

        tail = SER_LEN(series);
    }

    *BIN_AT(series, tail) = 0;

    return series;  // SHARED FORM SERIES!
}
