//
//  File: %call-windows.c
//  Summary: "Implemention of CALL native for Windows"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 Atronix Engineering
// Copyright 2012-2019 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except inbuf compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Windows has no clear standard on when piped processes return UTF-16 vs.
// ASCII, or UTF-8, or anything else.  It's just a pipe.  What programs do
// in general (including Rebol) is detect if they are hooked to a console
// with `GetFileType(GetStdHandle(STD_OUTPUT_HANDLE)) == FILE_TYPE_CHAR`.
// If so, they send UTF-16.
//
// https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-getfiletype
//
// If you call CMD.EXE itself and as it to perform a shell function, such as
// say `ECHO`, it will default to giving back ASCII.  This can be overridden
// with `CMD.EXE /U` ("when piped or redirected, gives "UCS-2 little endian")
//
// Given Windows itself wishing to set the standard for pipes and redirects
// to use plain bytes, it seems good to go with it.  Rather than give the
// appearance of endorsement of UCS-2/UTF-16 by offering a switch for it,
// a process returning it may be handled by requesting BINARY! output and
// then doing the conversion themselves.
//

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>

#include <process.h>
#include <shlobj.h>

#ifdef IS_ERROR
    #undef IS_ERROR  // %winerror.h defines, Rebol has a different meaning
#endif

#include "sys-core.h"

#include "tmp-mod-process.h"

#include "reb-process.h"


//
//  Call_Core: C
//
REB_R Call_Core(REBFRM *frame_) {
    PROCESS_INCLUDE_PARAMS_OF_CALL_INTERNAL_P;

    UNUSED(REF(console));  // !!! This is not paid attention to (?)

    Check_Security_Placeholder(Canon(SYM_CALL), SYM_EXEC, ARG(command));

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (IS_TEXT(ARG(output)) or IS_BINARY(ARG(output)))
        FAIL_IF_READ_ONLY(ARG(output));
    if (IS_TEXT(ARG(error)) or IS_BINARY(ARG(error)))
        FAIL_IF_READ_ONLY(ARG(error));

    bool flag_wait;
    if (
        REF(wait)
        or (
            IS_TEXT(ARG(input)) or IS_BINARY(ARG(input))
            or IS_TEXT(ARG(output)) or IS_BINARY(ARG(output))
            or IS_TEXT(ARG(error)) or IS_BINARY(ARG(error))
        )  // I/O redirection implies /WAIT
    ){
        flag_wait = true;
    }
    else
        flag_wait = false;

    // We synthesize the argc and argv from the "command".  This does dynamic
    // allocations of argc strings through the API, which need to be freed
    // before we return.
    //
    REBWCHAR *call;
    int argc;
    const REBWCHAR **argv;

    if (IS_TEXT(ARG(command))) {  // Windows takes command-lines by default

      text_command:

        call = rebSpellWideQ(ARG(command), rebEND);

        argc = 1;
        argv = rebAllocN(const REBWCHAR*, (argc + 1));

        // !!! Make two copies because it frees cmd and all the argv.  Review.
        //
        argv[0] = rebSpellWideQ(ARG(command), rebEND);
        argv[1] = nullptr;
    }
    else if (IS_BLOCK(ARG(command))) {
        //
        // In order for argv-call to work with Windows reliably, it has to do
        // proper escaping of its arguments when forming a string.  We
        // do this with a usermode helper.
        //
        // https://github.com/rebol/rebol-issues/issues/2225

        REBVAL *text = rebValue(
            "argv-block-to-command*", ARG(command),
        rebEND);
        Move_Value(ARG(command), text);
        rebRelease(text);
        goto text_command;
    }
    else
        fail (PAR(command));

    REBU64 pid = 1020;  // avoid uninitialized warning, garbage value
    DWORD exit_code = 304;  // ...same...

  #ifdef GET_IS_NT_FLAG  // !!! Why was this here?
    bool is_NT;
    OSVERSIONINFO info;
    GetVersionEx(&info);
    is_NT = info.dwPlatformId >= VER_PLATFORM_WIN32_NT;
  #endif

    REBINT result = -1;
    REBINT ret = 0;

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;

    STARTUPINFO si;
    si.cb = sizeof(si);
    si.lpReserved = nullptr;
    si.lpDesktop = nullptr;
    si.lpTitle = nullptr;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWNORMAL;
    si.cbReserved2 = 0;
    si.lpReserved2 = nullptr;

    HANDLE hOutputRead = 0;
    HANDLE hOutputWrite = 0;
    HANDLE hInputWrite = 0;
    HANDLE hInputRead = 0;
    HANDLE hErrorWrite = 0;
    HANDLE hErrorRead = 0;

    WCHAR *cmd = nullptr;

    UNUSED(REF(info));

    char *inbuf = nullptr;
    size_t inbuf_size = 0;
    char *outbuf = nullptr;
    size_t outbuf_used = 0;
    char *errbuf = nullptr;
    size_t errbuf_used = 0;

    //=//// INPUT SOURCE SETUP ////////////////////////////////////////////=//

    if (not REF(input)) {  // get stdin normally (usually from user console)
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    else switch (VAL_TYPE(ARG(input))) {
      case REB_BLANK:  // act like there's no console input available at all
        si.hStdInput = 0;
        break;

      case REB_TEXT: {  // feed standard input from TEXT!
        //
        // See notes at top of file about why UTF-16/UCS-2 are not used here.
        // Pipes and file reirects are generally understood in Windows to
        // *not* use those encodings, and transmit raw bytes.
        //
        inbuf_size = rebSpellIntoQ(nullptr, 0, ARG(input), rebEND);
        inbuf = rebAllocN(char, inbuf_size + 1);
        size_t check = rebSpellIntoQ(inbuf, inbuf_size, ARG(input), rebEND);
        assert(check == inbuf_size);
        UNUSED(check);
        goto input_via_buffer; }

      case REB_BINARY:  // feed standard input from BINARY! (full-band)
        inbuf = s_cast(rebBytes(&inbuf_size, ARG(input), rebEND));

      input_via_buffer:

        if (not CreatePipe(&hInputRead, &hInputWrite, nullptr, 0))
            goto stdin_error;

        if (not SetHandleInformation(
            hInputRead,  // make child side handle inheritable
            HANDLE_FLAG_INHERIT,
            HANDLE_FLAG_INHERIT
        )){
            goto stdin_error;
        }
        si.hStdInput = hInputRead;
        break;

      case REB_FILE: {  // feed standard input from file contents
        WCHAR *local_wide = rebSpellWideQ("file-to-local", ARG(input), rebEND);

        hInputRead = CreateFile(
            local_wide,
            GENERIC_READ,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            OPEN_EXISTING,  // creation disposition
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,  // flags
            nullptr  // template
        );
        si.hStdInput = hInputRead;

        rebFree(local_wide);

        inbuf = nullptr;
        inbuf_size = 0;
        break; }

      default:
        panic (ARG(input));
    }

    // !!! Output and Error code is nearly identical and should be factored
    // into a subroutine.

    //=//// OUTPUT SINK SETUP /////////////////////////////////////////////=//

    if (not REF(output)) {  // outbuf stdout normally (usually to console)
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    else switch (VAL_TYPE(ARG(output))) {
      case REB_BLANK:  // discard outbuf (e.g. don't print to stdout)
        si.hStdOutput = 0;
        break;

      case REB_TEXT:  // write stdout outbuf to pre-existing TEXT!
      case REB_BINARY:  // write stdout outbuf to pre-existing BINARY!
        if (not CreatePipe(&hOutputRead, &hOutputWrite, NULL, 0))
            goto stdout_error;

        // make child side handle inheritable
        //
        if (not SetHandleInformation(
            hOutputWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto stdout_error;
        }
        si.hStdOutput = hOutputWrite;
        break;

      case REB_FILE: {  // write stdout outbuf to file
        WCHAR *local_wide = rebSpellWideQ(
            "file-to-local", ARG(output),
        rebEND);

        si.hStdOutput = CreateFile(
            local_wide,
            GENERIC_WRITE,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            CREATE_NEW,  // creation disposition
            FILE_ATTRIBUTE_NORMAL,  // flag and attributes
            nullptr  // template
        );

        if (
            si.hStdOutput == INVALID_HANDLE_VALUE
            and GetLastError() == ERROR_FILE_EXISTS
        ){
            si.hStdOutput = CreateFile(
                local_wide,
                GENERIC_WRITE,  // desired mode
                0,  // shared mode
                &sa,  // security attributes
                OPEN_EXISTING,  // creation disposition
                FILE_ATTRIBUTE_NORMAL,  // flag and attributes
                nullptr  // template
            );
        }

        rebFree(local_wide);
        break; }

      default:
        panic (ARG(output));
    }

    //=//// ERROR SINK SETUP //////////////////////////////////////////////=//

    if (not REF(error)) {  // outbuf stderr normally (usually same as stdout)
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    else switch (VAL_TYPE(ARG(error))) {
      case REB_BLANK:  // suppress stderr outbuf entirely
        si.hStdError = 0;
        break;

      case REB_TEXT:  // write stderr outbuf to pre-existing TEXT!
      case REB_BINARY:  // write stderr outbuf to pre-existing BINARY!
        if (not CreatePipe(&hErrorRead, &hErrorWrite, NULL, 0))
            goto stderr_error;

        // make child side handle inheritable
        //
        if (not SetHandleInformation(
            hErrorWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto stderr_error;
        }
        si.hStdError = hErrorWrite;
        break;

      case REB_FILE: {  // write stderr outbuf to file
        WCHAR *local_wide = rebSpellWideQ(
            "file-to-local", ARG(output),
        rebEND);

        si.hStdError = CreateFile(
            local_wide,
            GENERIC_WRITE,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            CREATE_NEW,  // creation disposition
            FILE_ATTRIBUTE_NORMAL,  // flag and attributes
            nullptr  // template
        );

        if (
            si.hStdError == INVALID_HANDLE_VALUE
            and GetLastError() == ERROR_FILE_EXISTS
        ){
            si.hStdError = CreateFile(
                local_wide,
                GENERIC_WRITE,  // desired mode
                0,  // shared mode
                &sa,  // security attributes
                OPEN_EXISTING,  // creation disposition
                FILE_ATTRIBUTE_NORMAL,  // flag and attributes
                nullptr  // template
            );
        }

        rebFree(local_wide);
        break; }

      default:
        panic (ARG(error));
    }

    //=//// COMMAND AND ARGUMENTS SETUP ///////////////////////////////////=//

    if (REF(shell)) {
        //
        // Do not pass /U for UCS-2, see notes at top of file.
        //
        const WCHAR *sh = L"cmd.exe /C \"";  // Note: begin surround quotes

        REBCNT len = wcslen(sh) + wcslen(call)
            + 1  // terminal quote mark
            + 1;  // NUL terminator

        cmd = cast(WCHAR*, malloc(sizeof(WCHAR) * len));
        cmd[0] = L'\0';
        wcscat(cmd, sh);
        wcscat(cmd, call);

        wcscat(cmd, L"\"");  // Note: ends surround quotes
    }
    else {  // CreateProcess might write to this memory, duplicate to be safe
        cmd = _wcsdup(call);  // uses malloc()
    }

    PROCESS_INFORMATION pi;
    result = CreateProcess(
        nullptr,  // executable name
        cmd,  // command to execute
        nullptr,  // process security attributes
        nullptr,  // thread security attributes
        TRUE,  // inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE,  // creation flags
        nullptr,  // environment
        nullptr,  // current directory
        &si,  // startup information
        &pi  // process information
    );

    free(cmd);

    pid = pi.dwProcessId;

    if (hInputRead != nullptr)
        CloseHandle(hInputRead);

    if (hOutputWrite != nullptr)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != nullptr)
        CloseHandle(hErrorWrite);

    if (result != 0 and flag_wait) {  // Wait for termination
        HANDLE handles[3];
        int count = 0;
        DWORD outbuf_capacity = 0;
        DWORD errbuf_capacity = 0;

        if (hInputWrite and inbuf_size > 0) {
            handles[count++] = hInputWrite;
        }
        if (hOutputRead != NULL) {
            outbuf_capacity = BUF_SIZE_CHUNK;
            outbuf_used = 0;

            outbuf = cast(char*, malloc(outbuf_capacity));
            handles[count++] = hOutputRead;
        }
        if (hErrorRead != NULL) {
            errbuf_capacity = BUF_SIZE_CHUNK;
            errbuf_used = 0;

            errbuf = cast(char*, malloc(errbuf_capacity));
            handles[count++] = hErrorRead;
        }

        while (count > 0) {
            DWORD wait_result = WaitForMultipleObjects(
                count, handles, FALSE, INFINITE
            );

            // If we test wait_result >= WAIT_OBJECT_0 it will tell us "always
            // true" with -Wtype-limits, since WAIT_OBJECT_0 is 0.  Take that
            // comparison out, but add assert in case you're on an abstracted
            // Windows and it isn't 0 for that implementation.
            //
            assert(WAIT_OBJECT_0 == 0);
            if (wait_result < WAIT_OBJECT_0 + count) {
                int i = wait_result - WAIT_OBJECT_0;
                DWORD inbuf_pos = 0;
                DWORD n = 0;

                if (handles[i] == hInputWrite) {
                    if (not WriteFile(
                        hInputWrite,
                        cast(char*, inbuf) + inbuf_pos,
                        inbuf_size - inbuf_pos,
                        &n,
                        NULL
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        inbuf_pos += n;
                        if (inbuf_pos >= inbuf_size) {  // done with input
                            CloseHandle(hInputWrite);
                            hInputWrite = NULL;
                            rebFree(inbuf);
                            inbuf = nullptr;
                            if (i < count - 1) {
                                memmove(
                                    &handles[i],
                                    &handles[i + 1],
                                    (count - i - 1) * sizeof(HANDLE)
                                );
                            }
                            count--;
                        }
                    }
                }
                else if (handles[i] == hOutputRead) {
                    if (not ReadFile(
                        hOutputRead,
                        cast(char*, outbuf) + outbuf_used,
                        outbuf_capacity - outbuf_used,
                        &n,
                        nullptr
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        outbuf_used += n;
                        if (outbuf_used >= outbuf_capacity) {
                            outbuf_capacity += BUF_SIZE_CHUNK;
                            outbuf = cast(char*,
                                realloc(outbuf, outbuf_capacity)
                            );
                            if (outbuf == NULL)
                                goto kill;
                        }
                    }
                }
                else if (handles[i] == hErrorRead) {
                    if (not ReadFile(
                        hErrorRead,
                        cast(char*, errbuf) + errbuf_used,
                        errbuf_capacity - errbuf_used,
                        &n,
                        NULL
                    )){
                        if (i < count - 1) {
                            memmove(
                                &handles[i],
                                &handles[i + 1],
                                (count - i - 1) * sizeof(HANDLE)
                            );
                        }
                        count--;
                    }
                    else {
                        errbuf_used += n;
                        if (errbuf_used >= errbuf_capacity) {
                            errbuf_capacity += BUF_SIZE_CHUNK;
                            errbuf = cast(char*,
                                realloc(errbuf, errbuf_capacity)
                            );
                            if (errbuf == NULL)
                                goto kill;
                        }
                    }
                }
                else {
                    //printf("Error READ");
                    if (ret == 0)
                        ret = GetLastError();
                    goto kill;
                }
            }
            else if (wait_result == WAIT_FAILED) {
                //printf("Wait Failed\n");
                if (ret == 0)
                    ret = GetLastError();
                goto kill;
            }
            else {
                //printf("Wait returns unexpected result: %d\n", wait_result);
                if (ret == 0)
                    ret = GetLastError();
                goto kill;
            }
        }

        WaitForSingleObject(pi.hProcess, INFINITE);  // check result??

        GetExitCodeProcess(pi.hProcess, &exit_code);

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else if (result) {
        //
        // No wait, close handles to avoid leaks
        //
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    else {
        // CreateProcess failed
        ret = GetLastError();
    }

    goto cleanup;

  kill:

    if (TerminateProcess(pi.hProcess, 0)) {
        WaitForSingleObject(pi.hProcess, INFINITE);

        GetExitCodeProcess(pi.hProcess, &exit_code);
    }
    else if (ret == 0) {
        ret = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

  cleanup:

    if (hInputWrite != nullptr)
        CloseHandle(hInputWrite);

    if (hOutputRead != nullptr)
        CloseHandle(hOutputRead);

    if (hErrorRead != nullptr)
        CloseHandle(hErrorRead);

    if (IS_FILE(ARG(error)))
        CloseHandle(si.hStdError);

  stderr_error:

    if (IS_FILE(ARG(output)))
        CloseHandle(si.hStdOutput);

  stdout_error:

    if (IS_FILE(ARG(input)))
        CloseHandle(si.hStdInput);

  stdin_error:

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(m_cast(REBWCHAR*, argv[i]));

    if (call != NULL)
        rebFree(call);

    rebFree(m_cast(REBWCHAR**, argv));

    if (IS_TEXT(ARG(output))) {
        if (outbuf_used > 0) {  // not wide chars, see notes at top of file
            REBVAL *output_val = rebSizedText(outbuf, outbuf_used);
            rebElide("append", ARG(output), output_val, rebEND);
            rebRelease(output_val);
        }
    }
    else if (IS_BINARY(ARG(output))) {  // can text/binary both append binary?
        if (outbuf_used > 0) {
            REBVAL *output_val = rebSizedBinary(outbuf, outbuf_used);
            rebElide("append", ARG(output), output_val, rebEND);
            rebRelease(output_val);
        }
    }
    else
        assert(outbuf == nullptr);
    free(outbuf);  // legal for outbuf=nullptr

    if (IS_TEXT(ARG(error))) {
        if (errbuf_used > 0) {  // not wide chars, see notes at top of file
            REBVAL *error_val = rebSizedText(errbuf, errbuf_used);
            rebElide("append", ARG(error), error_val, rebEND);
            rebRelease(error_val);
        }
    } else if (IS_BINARY(ARG(error))) {  // can text/binary both append binary?
        if (errbuf_used > 0) {
            REBVAL *error_val = rebSizedBinary(errbuf, errbuf_used);
            rebElide("append", ARG(error), error_val, rebEND);
            rebRelease(error_val);
        }
    }
    else
        assert(errbuf == nullptr);
    free(errbuf);  // legal for errbuf=nullptr

    if (inbuf != nullptr)
        rebFree(inbuf);

    if (ret != 0)
        rebFail_OS (ret);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, nullptr, Canon(SYM_ID)), pid);
        if (REF(wait))
            Init_Integer(
                Append_Context(info, nullptr, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        return Init_Object(D_OUT, info);
    }

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        return Init_Integer(D_OUT, exit_code);

    return Init_Integer(D_OUT, pid);
}
