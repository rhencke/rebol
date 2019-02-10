//
//  File: %reb-process.h
//  Summary: "Header file for 'Process-oriented' extension module"
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


ATTRIBUTE_NO_RETURN
inline static void Fail_Permission_Denied(void) {
    rebJumps("fail {The process does not have enough permission}", rebEND);
}

ATTRIBUTE_NO_RETURN
inline static void Fail_No_Process(const REBVAL *arg) {
    rebJumps(
        "fail [{The target process (group) does not exist:}", arg, "]",
    rebEND);
}

#ifdef TO_WINDOWS
    ATTRIBUTE_NO_RETURN
    inline static void Fail_Terminate_Failed(DWORD err) {  // GetLastError()
        rebJumps(
            "fail [{Terminate failed with error number:}", rebI(err), "]",
        rebEND);
    }
#endif

// !!! %mod-process.c is now the last file that uses this cross platform OS
// character definition.  Excise as soon as possible.
//
#ifdef TO_WINDOWS
    #define OSCHR WCHAR
#else
    #define OSCHR char
#endif


//
//  rebValSpellingAllocOS: C
//
// This is used to pass a REBOL value string to an OS API.
// On Windows, the result is a wide-char pointer, but on Linux, its UTF-8.
// The returned pointer must be freed with OS_FREE.
//
inline static OSCHR *rebValSpellingAllocOS(const REBVAL *any_string)
{
  #ifdef OS_WIDE_CHAR
    return rebSpellW(any_string, rebEND);
  #else
    return rebSpell(any_string, rebEND);
  #endif
}


//
//  Append_OS_Str: C
//
// The data which came back from the piping interface may be UTF-8 on Linux,
// or WCHAR on windows.  Yet we want to append that data to an existing
// Rebol string, whose size may vary.
//
// !!! Note: With UTF-8 Everywhere as the native Rebol string format, it
// *might* be more efficient to try using that string's buffer...however
// there can be issues of permanent wasted space if the buffer is made too
// large and not shrunk.
//
inline static void Append_OS_Str(REBVAL *dest, const void *src, REBINT len)
{
  #ifdef TO_WINDOWS
    REBVAL *src_str = rebLengthedTextW(cast(const REBWCHAR*, src), len);
  #else
    REBVAL *src_str = rebSizedText(cast(const char*, src), len);
  #endif

    rebElide("append", dest, src_str, rebEND);

    rebRelease(src_str);
}

// !!! The original implementation of CALL from Atronix had to communicate
// between the CALL native (defined in the core) and the host routine
// OS_Create_Process, which was not designed to operate on Rebol types.
// Hence if the user was passing in a BINARY! to which the data for the
// standard out or standard error was to be saved, it was produced in full
// in a buffer and returned, then appended.  This wastes space when compared
// to just appending to the string or binary itself.  With CALL rethought
// as an extension with access to the internal API, this could be changed...
// though for the moment, a malloc()'d buffer is expanded independently by
// BUF_SIZE_CHUNK and returned to CALL.
//
#define BUF_SIZE_CHUNK 4096

int OS_Create_Process(
    REBFRM *frame_, // stopgap: allows access to CALL's ARG() and REF()
    const OSCHR *call,
    int argc,
    const OSCHR* argv[],
    bool flag_wait, // distinct from REF(wait)
    uint64_t *pid,
    int *exit_code,
    char *input,
    uint32_t input_len,
    char **output,
    uint32_t *output_len,
    char **err,
    uint32_t *err_len
);
