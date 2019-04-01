//
//  File: %d-print.c
//  Summary: "low-level console print interface"
//  Section: debug
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
// R3 is intended to run on fairly minimal devices, so this code may
// duplicate functions found in a typical C lib. That's why output
// never uses standard clib printf functions.
//

/*
        Print_OS... - low level OS output functions
        Out_...     - general console output functions
        Debug_...   - debug mode (trace) output functions
*/

#include "sys-core.h"

static REBREQ *Req_SIO;


/***********************************************************************
**
**  Lower Level Print Interface
**
***********************************************************************/

//
//  Startup_StdIO: C
//
void Startup_StdIO(void)
{
    Req_SIO = OS_MAKE_DEVREQ(RDI_STDIO);

    // !!! "The device is already open, so this call will just setup the
    // request fields properly.

    REBVAL *result = OS_DO_DEVICE(Req_SIO, RDC_OPEN);
    assert(result == NULL); // !!! API not initialized yet, "pending" is a lie
    UNUSED(result);
}


//
//  Shutdown_StdIO: C
//
void Shutdown_StdIO(void)
{
    // !!! There is no OS_FREE_DEVREQ.  Should there be?  Should this
    // include an OS_ABORT_DEVICE?
    //
    Free_Req(Req_SIO);
}


//
//  Print_OS_Line: C
//
// Print a new line.
//
void Print_OS_Line(void)
{
    // !!! Don't put const literal directly into mutable Req_SIO->data

    static REBYTE newline[] = "\n";

    Req(Req_SIO)->common.data = newline;
    Req(Req_SIO)->length = 1;
    Req(Req_SIO)->actual = 0;

    REBVAL *result = OS_DO_DEVICE(Req_SIO, RDC_WRITE);
    assert(result != NULL);
    assert(not IS_ERROR(result));
    rebRelease(result);
}


//
//  Prin_OS_String: C
//
// Print a string (with no line terminator).
//
// The encoding options are OPT_ENC_XXX flags OR'd together.
//
void Prin_OS_String(const REBYTE *utf8, REBSIZ size, REBFLGS opts)
{
    struct rebol_devreq *req = Req(Req_SIO);

    req->flags |= RRF_FLUSH;
    if (opts & OPT_ENC_RAW)
        req->modes &= ~RFM_TEXT;
    else
        req->modes |= RFM_TEXT;

    req->actual = 0;

    DECLARE_LOCAL (temp);
    SET_END(temp);

    // !!! The historical division of labor between the "core" and the "host"
    // is that the host doesn't know how to poll for cancellation.  So data
    // gets broken up into small batches and it's this loop that has access
    // to the core "Do_Signals_Throws" query.  Hence one can send a giant
    // string to the OS_DO_DEVICE with RDC_WRITE and be able to interrupt it,
    // even though that device request could block forever in theory.
    //
    // There may well be a better way to go about this.
    //
    req->common.data = m_cast(REBYTE*, utf8); // !!! promises to not write
    while (size > 0) {
        if (Do_Signals_Throws(temp))
            fail (Error_No_Catch_For_Throw(temp));

        assert(IS_END(temp));

        // !!! Req_SIO->length is actually the "size", e.g. number of bytes.
        //
        if (size <= 1024)
            req->length = size;
        else if (not (opts & OPT_ENC_RAW))
            req->length = 1024;
        else {
            // Correct for UTF-8 batching so we don't span an encoded
            // character, back off until we hit a valid leading character.
            // Start by scanning 4 bytes back since that's the longest valid
            // UTF-8 encoded character.
            //
            req->length = 1020;
            while ((req->common.data[req->length] & 0xC0) == 0x80)
                ++req->length;
            assert(req->length <= 1024);
        }

        OS_DO_DEVICE_SYNC(Req_SIO, RDC_WRITE);

        req->common.data += req->length;
        size -= req->length;
    }
}


//
//  Form_Hex_Pad: C
//
// Form integer hex string and pad width with zeros.  Does not insert a #.
//
void Form_Hex_Pad(
    REB_MOLD *mo,
    REBI64 val, // !!! was REBU64 in R3-Alpha, but code did sign comparisons!
    REBINT len
){
    REBYTE buffer[MAX_HEX_LEN + 4];
    REBYTE *bp = buffer + MAX_HEX_LEN + 1;

    REBI64 sgn = (val < 0) ? -1 : 0;

    len = MIN(len, MAX_HEX_LEN);
    *bp-- = 0;
    while (val != sgn && len > 0) {
        *bp-- = Hex_Digits[val & 0xf];
        val >>= 4;
        len--;
    }

    for (; len > 0; len--)
        *bp-- = (sgn != 0) ? 'F' : '0';

    for (++bp; *bp != '\0'; ++bp)
        Append_Codepoint(mo->series, *bp);
}


//
//  Form_Hex2: C
//
// Convert byte-sized int to xx format.
//
void Form_Hex2(REB_MOLD *mo, REBYTE b)
{
    Append_Codepoint(mo->series, Hex_Digits[(b & 0xf0) >> 4]);
    Append_Codepoint(mo->series, Hex_Digits[b & 0xf]);
}


//
//  Form_Hex_Esc: C
//
// Convert byte to %xx format
//
void Form_Hex_Esc(REB_MOLD *mo, REBYTE b)
{
    Append_Codepoint(mo->series, '%');
    Append_Codepoint(mo->series, Hex_Digits[(b & 0xf0) >> 4]);
    Append_Codepoint(mo->series, Hex_Digits[b & 0xf]);
}


//
//  Form_RGBA: C
//
// Convert 32 bit RGBA to xxxxxx format.
//
void Form_RGBA(REB_MOLD *mo, const REBYTE *dp)
{
    REBCNT len_old = STR_LEN(mo->series);
    REBSIZ used_old = STR_SIZE(mo->series);

    EXPAND_SERIES_TAIL(SER(mo->series), 8);  // grow by 8 bytes, may realloc

    REBYTE *bp = BIN_AT(SER(mo->series), used_old);  // potentially new buffer

    bp[0] = Hex_Digits[(dp[0] >> 4) & 0xf];
    bp[1] = Hex_Digits[dp[0] & 0xf];
    bp[2] = Hex_Digits[(dp[1] >> 4) & 0xf];
    bp[3] = Hex_Digits[dp[1] & 0xf];
    bp[4] = Hex_Digits[(dp[2] >> 4) & 0xf];
    bp[5] = Hex_Digits[dp[2] & 0xf];
    bp[6] = Hex_Digits[(dp[3] >> 4) & 0xf];
    bp[7] = Hex_Digits[dp[3] & 0xf];
    bp[8] = '\0';

    TERM_STR_LEN_SIZE(mo->series, len_old + 8, used_old + 8);
}


//
//  Startup_Raw_Print: C
//
// Initialize print module.
//
void Startup_Raw_Print(void)
{
    TG_Byte_Buf = Make_Binary(1000);
}


//
//  Shutdown_Raw_Print: C
//
void Shutdown_Raw_Print(void)
{
    Free_Unmanaged_Series(TG_Byte_Buf);
    TG_Byte_Buf = NULL;
}
