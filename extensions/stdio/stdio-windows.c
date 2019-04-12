//
//  File: %dev-stdio.c
//  Summary: "Device: Standard I/O for Win32"
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
// Provides basic I/O streams support for redirection and
// opening a console window if necessary.
//

#include <windows.h>
#undef IS_ERROR

// !!! Read_IO writes directly into a BINARY!, whose size it needs to keep up
// to date (in order to have it properly terminated and please the GC).  At
// the moment it does this with the internal API, though libRebol should
// hopefully suffice in the future.  This is part of an ongoing effort to
// make the device layer work more in the vocabulary of Rebol types.
//
#include "sys-core.h"

static HANDLE Stdout_Handle = nullptr;
static HANDLE Stdin_Handle = nullptr;

// While pipes and redirected files in Windows do raw bytes, the console
// uses UTF-16.  The calling layer expects UTF-8 back, so the Windows API
// for conversion is used.  The UTF-16 data must be held in a buffer.
//
#define WCHAR_BUF_CAPACITY (16 * 1024)
static WCHAR *Wchar_Buf = nullptr;

static BOOL Redir_Out = 0;
static BOOL Redir_Inp = 0;

//**********************************************************************


static void Close_Stdio(void)
{
    if (Wchar_Buf) {
        free(Wchar_Buf);
        Wchar_Buf = nullptr;
    }
}


//
//  Quit_IO: C
//
DEVICE_CMD Quit_IO(REBREQ *dr)
{
    REBDEV *dev = (REBDEV*)dr; // just to keep compiler happy above

    Close_Stdio();
    dev->flags &= ~RDF_OPEN;
    return DR_DONE;
}


//
//  Open_IO: C
//
DEVICE_CMD Open_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);

    REBDEV *dev = req->device;

    // Avoid opening the console twice (compare dev and req flags):
    if (dev->flags & RDF_OPEN) {
        // Device was opened earlier as null, so req must have that flag:
        if (dev->flags & SF_DEV_NULL)
            req->modes |= RDM_NULL;
        req->flags |= RRF_OPEN;
        return DR_DONE; // Do not do it again
    }

    if (not (req->modes & RDM_NULL)) {
        // Get the raw stdio handles:
        Stdout_Handle = GetStdHandle(STD_OUTPUT_HANDLE);
        Stdin_Handle = GetStdHandle(STD_INPUT_HANDLE);
        //StdErr_Handle = GetStdHandle(STD_ERROR_HANDLE);

        Redir_Out = (GetFileType(Stdout_Handle) != FILE_TYPE_CHAR);
        Redir_Inp = (GetFileType(Stdin_Handle) != FILE_TYPE_CHAR);

        if (not Redir_Inp or not Redir_Out) {
            //
            // If either input or output is not redirected, preallocate
            // a buffer for conversion from/to UTF-8.
            //
            Wchar_Buf = cast(WCHAR*,
                malloc(sizeof(WCHAR) * WCHAR_BUF_CAPACITY)
            );
        }

        if (not Redir_Inp) {
            //
            // Windows offers its own "smart" line editor (with history
            // management, etc.) in the form of the Windows Terminal.  These
            // modes only apply if a the input is coming from the terminal,
            // not if Rebol has a file redirection connected to the input.
            //
            // While the line editor is running with ENABLE_LINE_INPUT, there
            // are very few hooks offered.  (See remarks on ReadConsole() call
            // about how even being able to terminate the input with escape
            // is not possible--much less reading function keys, etc.)  For
            // the moment, delegating the editing process to proven code
            // built into the OS is considered worth it for the limitations in
            // the console client--given development priorities.
            //
            SetConsoleMode(
                Stdin_Handle,
                ENABLE_LINE_INPUT
                    | ENABLE_PROCESSED_INPUT
                    | ENABLE_ECHO_INPUT
                    | 0x0080  // ENABLE_EXTENDED_FLAGS (quick edit/insert)
                    | 0x0040  // quick edit (not defined in VC6)
                    | 0x0020  // quick insert (not defined in VC6)
            );
        }
    }
    else
        dev->flags |= SF_DEV_NULL;

    req->flags |= RRF_OPEN;
    dev->flags |= RDF_OPEN;

    return DR_DONE;
}


//
//  Close_IO: C
//
DEVICE_CMD Close_IO(REBREQ *req)
{
    REBDEV *dev = Req(req)->device;

    Close_Stdio();

    dev->flags &= ~RRF_OPEN;

    return DR_DONE;
}


//
//  Write_IO: C
//
// Low level "raw" standard output function.
//
// Allowed to restrict the write to a max OS buffer size.
//
// Returns the number of chars written.
//
DEVICE_CMD Write_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);

    if (req->modes & RDM_NULL) {
        req->actual = req->length;
        return DR_DONE;
    }

    if (Stdout_Handle == nullptr)
        return DR_DONE;

    if (Redir_Out) {
        if (req->modes & RFM_TEXT) {
            //
            // Writing UTF-8 text.  Currently no actual check is done to make
            // sure that it's valid UTF-8, even invalid bytes would be written
            // but this could be changed.
        }

        // !!! Historically, Rebol on Windows automatically "enlined" strings
        // on write to turn LF to CR LF.  This was done in Prin_OS_String().
        // However, the current idea is to be more prescriptive and not
        // support this without a special codec.  In lieu of a more efficient
        // codec method, those wishing to get CR LF will need to manually
        // enline, or ADAPT their WRITE to do this automatically.
        //
        // Note that redirection on Windows does not use UTF-16 typically.
        // Even CMD.EXE requires a /U switch to do so.

        DWORD total_bytes;
        BOOL ok = WriteFile(
            Stdout_Handle,
            req->common.data,
            req->length,
            &total_bytes,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());
        UNUSED(total_bytes);
    }
    else {  // not redirected, so being sent to the console
        if (req->modes & RFM_TEXT) {
            //
            // Convert UTF-8 buffer to Win32 wide-char format for console.
            // When not redirected, the default seems to be able to translate
            // LF to CR LF automatically (assuming that's what you wanted).
            //
            DWORD len = MultiByteToWideChar(
                CP_UTF8,
                0,
                s_cast(req->common.data),
                req->length,
                Wchar_Buf,
                WCHAR_BUF_CAPACITY
            );
            if (len > 0) { // no error
                DWORD total_wide_chars;
                BOOL ok = WriteConsoleW(
                    Stdout_Handle,
                    Wchar_Buf,
                    len,
                    &total_wide_chars,
                    0
                );
                if (not ok)
                    rebFail_OS (GetLastError());
                UNUSED(total_wide_chars);
            }
        }
        else {
            // !!! Writing a BINARY! to a redirected console, e.g. a CGI
            // script, makes sense--e.g. it might be some full bandwidth data
            // being downloaded that's neither UTF-8 nor UTF-16.  And it makes
            // some sense on UNIX, as the terminal will just have to figure
            // out what to do with those bytes.  But on Windows, there's a
            // problem...since the console API takes wide characters.
            //
            // We *could* assume the user meant to write UTF-16 data, and only
            // fail if it's an odd number of bytes.  But that means that the
            // write of the BINARY! would have different meanings if directed
            // at a file as opposed to not redirected.  If there was a true
            // need to write UTF-16 data directly to the console, that should
            // be a distinct console-oriented function.
            //
            // Instead, we can do something like change the color and write
            // out some information.  Ideally this would be something like
            // the data in hexadecimal, but since this is a niche leave it
            // as a placeholder.
            //
            // !!! The caller currently breaks up binary data into chunks to
            // pass in order to handle cancellation, so that should also be
            // taken into account.

            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(Stdout_Handle, &csbi);  // save color

            SetConsoleTextAttribute(
                Stdout_Handle,
                BACKGROUND_GREEN | FOREGROUND_BLUE
            );

            WCHAR message[] = L"Binary Data Sent to Non-Redirected Console";

            DWORD total_wide_chars;
            BOOL ok = WriteConsoleW(
                Stdout_Handle,
                message,
                wcslen(message),  // wants wide character count
                &total_wide_chars,
                0
            );
            SetConsoleTextAttribute(
                Stdout_Handle,
                csbi.wAttributes  // restore these attributes
            );

            if (not ok)
                rebFail_OS (GetLastError());
            UNUSED(total_wide_chars);
        }
    }

    req->actual = req->length;  // want byte count written, assume success

    // !!! There was some code in R3-Alpha here which checked req->flags for
    // "RRF_FLUSH" and would flush, but it was commented out (?)

    return DR_DONE;
}


//
//  Read_IO: C
//
// Low level "raw" standard input function.
//
// The request buffer must be long enough to hold result.
//
// Result is NOT terminated (the actual field has length.)
//
DEVICE_CMD Read_IO(REBREQ *io)
{
    struct rebol_devreq *req = Req(io);
    assert(req->length >= 2);  // abort is signaled with (ESC '\0')

    // !!! While transitioning away from the R3-Alpha "abstract OS" model,
    // this hook now receives a BINARY! in req->text which it is expected to
    // fill with UTF-8 data, with req->length bytes.
    //
    assert(VAL_INDEX(req->common.binary) == 0);
    assert(VAL_LEN_AT(req->common.binary) == 0);

    REBSER *bin = VAL_BINARY(req->common.binary);
    assert(SER_AVAIL(bin) >= req->length);

    if (req->modes & RDM_NULL) {
        TERM_BIN_LEN(bin, 0);
        return DR_DONE;
    }

    if (Stdin_Handle == nullptr) {
        TERM_BIN_LEN(bin, 0);
        return DR_DONE;
    }

    if (Redir_Inp) {  // always UTF-8
        DWORD total;
        BOOL ok = ReadFile(
            Stdin_Handle,
            BIN_HEAD(bin),
            req->length,
            &total,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());

        TERM_BIN_LEN(bin, total);
        return DR_DONE;
    }

    // !!! ReadConsole() in the ENABLE_LINE_INPUT mode is a terribly limited
    // API, and if you don't use that mode you are basically completely on
    // your own for line editing (backspace, cursoring, etc.)  It's all or
    // nothing--there's no way to hook it--and you can't even tell if an
    // escape is pressed...it always clears to the beginning of line.
    //
    // There might seem to be some hope in the CONSOLE_READCONSOLE_CONTROL
    // parameter.  The structure is horribly documented on MSDN, but it is
    // supposed to offer a way to register some control keys to break out of
    // the input besides a completing newline.  It turns out dwCtrlWakeupMask
    // is (supposedly) a bit mask of 0-31 ASCII points for control characters:
    //
    // https://stackoverflow.com/a/43836992/211160
    //
    // Theory is that with ENABLE_LINE_INPUT, a successfully completed line
    // will always end in CR LF for a `total` of at least 2.  Then if
    // `dwCtrlWakeupMask` is registered for a key, and `nInitialChars` is
    // set to 0 (preserve nothing), the fact that the user terminated with the
    // control key *should* be detectable by `total == 0`.
    //
    // But as mentioned, masking escape in as (1 << 27) has no effect.  And
    // when using ENABLE_PROCESSED_INPUT (which you must in order to get the
    // backspace/etc. behavior in the line editor) then Ctrl-C will exit
    // ReadConsole() call and return a total of 0...regardless of whether you
    // mask (1 << 3) or not.  It also exits before the SetConsoleCtrlHandler()
    // does for handling CTRL_C_EVENT.  :-/
    //
    // Then Ctrl-D can be in the mask.  It does indeed exit the read when it
    // is hit, but ignores `nInitialChars` and just sticks a codepoint of 4
    // (^D) wherever the cursor is!!!
    //
    // As awful as this all sounds, it actually can be manipulated to give
    // three different outcomes.  It's just rather rickety-seeming, but the
    // odds are this all comes from bend-over-backward legacy support of
    // things that couldn't be changed to be better...so it will probably
    // be working this way for however long Win32 stays relevant.
    //
    // For the moment, having Ctrl-D instead of escape for abort input (vs.
    // abort script) is accepted as the price paid, to delegate the Unicode
    // aware cursoring/backspacing/line-editing to the OS.  Which also means
    // a smaller executable than trying to rewrite it oneself.

  #ifdef PRE_VISTA
    LPVOID pInputControl = NULL;
  #else
    CONSOLE_READCONSOLE_CONTROL ctl;  // Unavailable pre-Vista, e.g. Mingw32
    PCONSOLE_READCONSOLE_CONTROL pInputControl = &ctl;

    ctl.nLength = sizeof(CONSOLE_READCONSOLE_CONTROL);
    ctl.nInitialChars = 0;  // when hit, empty buffer...no CR LF
    ctl.dwCtrlWakeupMask = (1 << 4);  // ^D (^C is implicit)
    ctl.dwControlKeyState = 0;  // no alt+shift modifiers (beyond ctrl)
  #endif

    DWORD total;
    BOOL ok = ReadConsoleW(
        Stdin_Handle,
        Wchar_Buf,
        WCHAR_BUF_CAPACITY - 1,
        &total,
        pInputControl
    );
    if (not ok)
        rebFail_OS (GetLastError());

    // Ctrl-C and Ctrl-D will terminate input without the newline that is
    // expected by code calling INPUT.  If these forms of cancellation are
    // encountered, we write a line to maintain the visual invariant.
    //
    WCHAR cr_lf_term[3];
    cr_lf_term[0] = '\r';  // CR
    cr_lf_term[1] = '\n';  // LF
    cr_lf_term[2] = '\0';

    if (total == 0) {
        //
        // Has to be a Ctrl-C, because it returns 0 total.  There is no
        // apparent way to avoid this behavior a priori, nor to resume the
        // console operation as if nothing had happened.
        //
        // Given that, write compensating line.  !!! Check error?
        //
        WriteConsoleW(Stdout_Handle, cr_lf_term, 2, NULL, 0);

        // The Ctrl-C will be passed on to the SetConsoleCtrlHandler().
        // Regardless of what the Ctrl-C event does (it runs on its own thread
        // in a console app) we'll get here, and have to return *something*
        // to INPUT or whoever called.
        //
        // Give a zero length output.  If halting was enabled, further Rebol
        // code of INPUT should not run.  In the case that INPUT sees this
        // signal and a halt does not happen, it will FAIL.  Only special
        // clients which can run with no cancellability (HOST-CONSOLE)
        // should trap it and figure out what to do with the non-ideal state.
        //
        TERM_BIN_LEN(bin, 0);
        return DR_DONE;
    }

    DWORD i;
    for (i = 0; i < total; ++i) {
        if (Wchar_Buf[i] == 4) {
            //
            // A Ctrl-D poked in at any position means escape.  Return it
            // as a single-character null terminated string of escape.
            //
            *BIN_AT(bin, 0) = ESC;
            TERM_BIN_LEN(bin, 0);

            // Write compensating line.  !!! Check error?
            //
            WriteConsoleW(Stdout_Handle, cr_lf_term, 2, NULL, 0);
            return DR_DONE;
        }
    }

    // The format given back is expected to be UTF-8 with no carriage returns.
    // Convert terminal CR LF to plain LF.
    //
    if (
        total >= 2
        and Wchar_Buf[total - 2] == '\r' and Wchar_Buf[total - 1] == '\n'
    ){
        Wchar_Buf[total - 2] = '\n';
        Wchar_Buf[total - 1] = '\0';
        --total;
    }

    DWORD encoded_len = WideCharToMultiByte(
        CP_UTF8,
        0,
        Wchar_Buf,
        total,
        s_cast(BIN_HEAD(bin)),
        req->length,
        0,
        0
    );

    // Note: WideCharToMultibyte would fail if cchWideChar was 0.  (we
    // know total is *not* 0 as it was handled above.)  In any case, a 0
    // result for the encoded length is how errors are signaled, as it could
    // not happen any other way.
    //
    if (encoded_len == 0)
        rebFail_OS (GetLastError());

    TERM_BIN_LEN(bin, encoded_len);
    return DR_DONE;
}


/***********************************************************************
**
**  Command Dispatch Table (RDC_ enum order)
**
***********************************************************************/

static DEVICE_CMD_CFUNC Dev_Cmds[RDC_MAX] =
{
    0,  // init
    Quit_IO,
    Open_IO,
    Close_IO,
    Read_IO,
    Write_IO,
    0,  // connect
    0,  // query
    0,  // modify
    0,  // CREATE was once used for opening echo file
};

DEFINE_DEV(
    Dev_StdIO,
    "Standard IO", 1, Dev_Cmds, RDC_MAX, sizeof(struct rebol_devreq)
);

