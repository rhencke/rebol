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
//  OS_Create_Process: C
//
// Return -1 on error.
//
int OS_Create_Process(
    REBFRM *frame_,  // stopgap: allows access to CALL's ARG() and REF()
    const WCHAR *call,
    int argc,
    const WCHAR * argv[],
    bool flag_wait,
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
){
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    UNUSED(ARG(command));  // turned into `call` and `argv/argc` by CALL
    UNUSED(REF(wait));  // covered by flag_wait

    UNUSED(REF(console));  // actually not paid attention to

    if (call == NULL)
        fail ("'argv[]'-style launching not implemented on Windows CALL");

  #ifdef GET_IS_NT_FLAG  // !!! Why was this here?
    bool is_NT;
    OSVERSIONINFO info;
    GetVersionEx(&info);
    is_NT = info.dwPlatformId >= VER_PLATFORM_WIN32_NT;
  #endif

    UNUSED(argc);
    UNUSED(argv);

    REBINT result = -1;
    REBINT ret = 0;

    HANDLE hOutputRead = 0;
    HANDLE hOutputWrite = 0;
    HANDLE hInputWrite = 0;
    HANDLE hInputRead = 0;
    HANDLE hErrorWrite = 0;
    HANDLE hErrorRead = 0;

    WCHAR *cmd = NULL;
    char *oem_input = NULL;

    UNUSED(REF(info));

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    STARTUPINFO si;
    si.cb = sizeof(si);
    si.lpReserved = NULL;
    si.lpDesktop = NULL;
    si.lpTitle = NULL;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.wShowWindow = SW_SHOWNORMAL;
    si.cbReserved2 = 0;
    si.lpReserved2 = NULL;

    if (not REF(input)) {  // get stdin normally (usually from user console) 
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    else switch (VAL_TYPE(ARG(in))) {
      case REB_BLANK:  // act like there's no console input available at all
        si.hStdInput = 0;
        break;

      case REB_TEXT:  // feed standard input from TEXT! (as UTF16 on Win)
      case REB_BINARY:  // feed standard input from BINARY! (full-band)
        if (!CreatePipe(&hInputRead, &hInputWrite, NULL, 0)) {
            goto input_error;
        }

        // make child side handle inheritable
        if (!SetHandleInformation(
            hInputRead, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto input_error;
        }
        si.hStdInput = hInputRead;
        break;

      case REB_FILE: {  // feed standard input from file contents
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(in), rebEND);

        hInputRead = CreateFile(
            local_wide,
            GENERIC_READ,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            OPEN_EXISTING,  // creation disposition
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,  // flags
            NULL  // template
        );
        si.hStdInput = hInputRead;

        rebFree(local_wide);
        break; }

      default:
        panic (ARG(in));
    }

    if (not REF(output)) {  // output stdout normally (usually to console)
        si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    else switch (VAL_TYPE(ARG(out))) {
      case REB_BLANK:  // discard output (e.g. don't print to stdout)
        si.hStdOutput = 0;
        break;

      case REB_TEXT:  // write stdout output to pre-existing TEXT!
      case REB_BINARY:  // write stdout output to pre-existing BINARY!
        if (!CreatePipe(&hOutputRead, &hOutputWrite, NULL, 0)) {
            goto output_error;
        }

        // make child side handle inheritable
        //
        if (!SetHandleInformation(
            hOutputWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto output_error;
        }
        si.hStdOutput = hOutputWrite;
        break;

      case REB_FILE: {  // write stdout output to file
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(out), rebEND);

        si.hStdOutput = CreateFile(
            local_wide,
            GENERIC_WRITE,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            CREATE_NEW,  // creation disposition
            FILE_ATTRIBUTE_NORMAL,  // flag and attributes
            NULL  // template
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
                NULL  // template
            );
        }

        rebFree(local_wide);
        break; }

      default:
        panic (ARG(out));
    }

    if (not REF(error)) {  // output stderr normally (usually same as stdout)
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    }
    else switch (VAL_TYPE(ARG(err))) {
      case REB_BLANK:  // suppress stderr output entirely
        si.hStdError = 0;
        break;

      case REB_TEXT:  // write stderr output to pre-existing TEXT!
      case REB_BINARY:  // write stderr output to pre-existing BINARY!
        if (!CreatePipe(&hErrorRead, &hErrorWrite, NULL, 0)) {
            goto error_error;
        }

        // make child side handle inheritable
        //
        if (!SetHandleInformation(
            hErrorWrite, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT
        )){
            goto error_error;
        }
        si.hStdError = hErrorWrite;
        break;

      case REB_FILE: {  // write stderr output to file
        WCHAR *local_wide = rebSpellW("file-to-local", ARG(out), rebEND);

        si.hStdError = CreateFile(
            local_wide,
            GENERIC_WRITE,  // desired mode
            0,  // shared mode
            &sa,  // security attributes
            CREATE_NEW,  // creation disposition
            FILE_ATTRIBUTE_NORMAL,  // flag and attributes
            NULL  // template
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
                NULL  // template
            );
        }

        rebFree(local_wide);
        break; }

      default:
        panic (ARG(err));
    }

    if (REF(shell)) {
        const WCHAR *sh = L"cmd.exe /C \"";  // Note: begins surround quotes 

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
        NULL,  // executable name
        cmd,  // command to execute
        NULL,  // process security attributes
        NULL,  // thread security attributes
        TRUE,  // inherit handles, must be TRUE for I/O redirection
        NORMAL_PRIORITY_CLASS | CREATE_DEFAULT_ERROR_MODE,  // creation flags
        NULL,  // environment
        NULL,  // current directory
        &si,  // startup information
        &pi  // process information
    );

    free(cmd);

    *pid = pi.dwProcessId;

    if (hInputRead != NULL)
        CloseHandle(hInputRead);

    if (hOutputWrite != NULL)
        CloseHandle(hOutputWrite);

    if (hErrorWrite != NULL)
        CloseHandle(hErrorWrite);

    if (result != 0 and flag_wait) {  // Wait for termination
        HANDLE handles[3];
        int count = 0;
        DWORD output_size = 0;
        DWORD err_size = 0;

        if (hInputWrite and input_len > 0) {
            if (IS_TEXT(ARG(in))) {
                DWORD dest_len = 0;
                // convert input encoding from UNICODE to OEM //
                // !!! Is cast to WCHAR here legal?
                dest_len = WideCharToMultiByte(
                    CP_OEMCP,
                    0,
                    cast(WCHAR*, input),
                    input_len,
                    oem_input,
                    dest_len,
                    NULL,
                    NULL
                );
                if (dest_len > 0) {
                    oem_input = cast(char*, malloc(dest_len));
                    if (oem_input != NULL) {
                        WideCharToMultiByte(
                            CP_OEMCP,
                            0,
                            cast(WCHAR*, input),
                            input_len,
                            oem_input,
                            dest_len,
                            NULL,
                            NULL
                        );
                        input_len = dest_len;
                        input = oem_input;
                        handles[count ++] = hInputWrite;
                    }
                }
            } else {
                assert(IS_BINARY(ARG(in)));
                handles[count ++] = hInputWrite;
           } 
        }
        if (hOutputRead != NULL) {
            output_size = BUF_SIZE_CHUNK;
            *output_len = 0;

            *output = cast(char*, malloc(output_size));
            handles[count ++] = hOutputRead;
        }
        if (hErrorRead != NULL) {
            err_size = BUF_SIZE_CHUNK;
            *err_len = 0;

            *err = cast(char*, malloc(err_size));
            handles[count++] = hErrorRead;
        }

        while (count > 0) {
            DWORD wait_result = WaitForMultipleObjects(
                count, handles, FALSE, INFINITE
            );

            // If we test wait_result >= WAIT_OBJECT_0 it will tell us "always
            // true" with -Wtype-limits, since WAIT_OBJECT_0 is 0.  Take that
            // comparison out but add assert in case you're on some abstracted
            // Windows and it isn't 0 for that implementation.
            //
            assert(WAIT_OBJECT_0 == 0);
            if (wait_result < WAIT_OBJECT_0 + count) {
                int i = wait_result - WAIT_OBJECT_0;
                DWORD input_pos = 0;
                DWORD n = 0;

                if (handles[i] == hInputWrite) {
                    if (!WriteFile(
                        hInputWrite,
                        cast(char*, input) + input_pos,
                        input_len - input_pos,
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
                        input_pos += n;
                        if (input_pos >= input_len) {  // done with input
                            CloseHandle(hInputWrite);
                            hInputWrite = NULL;
                            free(oem_input);
                            oem_input = NULL;
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
                    if (!ReadFile(
                        hOutputRead,
                        *cast(char**, output) + *output_len,
                        output_size - *output_len,
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
                        *output_len += n;
                        if (*output_len >= output_size) {
                            output_size += BUF_SIZE_CHUNK;
                            *output = cast(char*,
                                realloc(*output, output_size)
                            );
                            if (*output == NULL)
                                goto kill;
                        }
                    }
                }
                else if (handles[i] == hErrorRead) {
                    if (!ReadFile(
                        hErrorRead,
                        *cast(char**, err) + *err_len,
                        err_size - *err_len,
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
                        *err_len += n;
                        if (*err_len >= err_size) {
                            err_size += BUF_SIZE_CHUNK;
                            *err = cast(char*, realloc(*err, err_size));
                            if (*err == NULL) goto kill;
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

        DWORD temp;
        GetExitCodeProcess(pi.hProcess, &temp);
        *exit_code = temp;

        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        if (IS_TEXT(ARG(out)) and *output and *output_len > 0) {
            // convert to wide char string
            int dest_len = 0;
            WCHAR *dest = NULL;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*output);
                *output = NULL;
                *output_len = 0;
            }
            dest = cast(WCHAR*, malloc(*output_len * sizeof(WCHAR)));
            if (dest == NULL)
                goto cleanup;
            MultiByteToWideChar(
                CP_OEMCP, 0, *output, *output_len, dest, dest_len
            );
            free(*output);
            *output = cast(char*, dest);
            *output_len = dest_len;
        }

        if (IS_TEXT(ARG(err)) and *err != nullptr and *err_len > 0) {
            // convert to wide char string
            int dest_len = 0;
            WCHAR *dest = NULL;
            dest_len = MultiByteToWideChar(
                CP_OEMCP, 0, *err, *err_len, dest, 0
            );
            if (dest_len <= 0) {
                free(*err);
                *err = NULL;
                *err_len = 0;
            }
            dest = cast(WCHAR*, malloc(*err_len * sizeof(WCHAR)));
            if (dest == NULL) goto cleanup;
            MultiByteToWideChar(CP_OEMCP, 0, *err, *err_len, dest, dest_len);
            free(*err);
            *err = cast(char*, dest);
            *err_len = dest_len;
        }
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

        DWORD temp;
        GetExitCodeProcess(pi.hProcess, &temp);
        *exit_code = temp;
    }
    else if (ret == 0) {
        ret = GetLastError();
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

  cleanup:

    if (oem_input != NULL)
        free(oem_input);
  
    if (output and *output and *output_len == 0)
        free(*output);

    if (err and *err != NULL and *err_len == 0)
        free(*err);

    if (hInputWrite != NULL)
        CloseHandle(hInputWrite);

    if (hOutputRead != NULL)
        CloseHandle(hOutputRead);

    if (hErrorRead != NULL)
        CloseHandle(hErrorRead);

    if (IS_FILE(ARG(err)))
        CloseHandle(si.hStdError);

  error_error:

    if (IS_FILE(ARG(out)))
        CloseHandle(si.hStdOutput);

  output_error:

    if (IS_FILE(ARG(in)))
        CloseHandle(si.hStdInput);

  input_error:

    return ret;  // meaning depends on flags
}
