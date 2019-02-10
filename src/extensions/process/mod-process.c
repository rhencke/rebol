//
//  File: %mod-call.c
//  Summary: "Native Functions for spawning and controlling processes"
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

#ifdef TO_WINDOWS
    #include <windows.h>
    #include <process.h>
    #include <shlobj.h>

    #ifdef IS_ERROR
        #undef IS_ERROR //winerror.h defines, Rebol has a different meaning
    #endif
#else
    #if !defined(__cplusplus) && defined(TO_LINUX)
        //
        // See feature_test_macros(7), this definition is redundant under C++
        //
        #define _GNU_SOURCE // Needed for pipe2 when #including <unistd.h>
    #endif
    #include <unistd.h>
    #include <stdlib.h>

    // The location of "environ" (environment variables inventory that you
    // can walk on POSIX) can vary.  Some put it in stdlib, some put it
    // in <unistd.h>.  And OS X doesn't define it in a header at all, you
    // just have to declare it yourself.  :-/
    //
    // https://stackoverflow.com/a/31347357/211160
    //
    #if defined(TO_OSX) || defined(TO_OPENBSD_X64)
        extern char **environ;
    #endif

    #include <errno.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <signal.h>
    #include <sys/stat.h>
    #include <sys/wait.h>
    #if !defined(WIFCONTINUED) && defined(TO_ANDROID)
    // old version of bionic doesn't define WIFCONTINUED
    // https://android.googlesource.com/platform/bionic/+/c6043f6b27dc8961890fed12ddb5d99622204d6d%5E%21/#F0
        # define WIFCONTINUED(x) (WIFSTOPPED(x) && WSTOPSIG(x) == 0xffff)
    #endif
#endif

#include "sys-core.h"

#include "tmp-mod-process.h"

#include "reb-process.h"


//
//  export call: native [
//
//  {Run another program; return immediately (unless /WAIT)}
//
//      command "OS-local command line, block with arguments, executable file"
//          [text! block! file!]
//      /wait "Wait for command to terminate before returning"
//      /console "Runs command with I/O redirected to console"
//      /shell "Forces command to be run from shell"
//      /info "Returns process information object"
//      /input "Redirects stdin to in (if blank, /dev/null)"
//      in [text! binary! file! blank!]
//      /output "Redirects stdout to out (if blank, /dev/null)"
//      out [text! binary! file! blank!]
//      /error "Redirects stderr to err (if blank, /dev/null)"
//      err [text! binary! file! blank!]
//  ]
//
REBNATIVE(call)
//
// !!! Parameter usage may require WAIT mode even if not explicitly requested.
// /WAIT should be default, with /ASYNC (or otherwise) as exception!
{
    PROCESS_INCLUDE_PARAMS_OF_CALL;

    // SECURE was never actually done for R3-Alpha
    //
    Check_Security(Canon(SYM_CALL), POL_EXEC, ARG(command));

    // Make sure that if the output or error series are STRING! or BINARY!,
    // they are not read-only, before we try appending to them.
    //
    if (IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)))
        FAIL_IF_READ_ONLY_SERIES(ARG(out));
    if (IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)))
        FAIL_IF_READ_ONLY_SERIES(ARG(err));

    // !!! This is pretty much about it for what CALL can do in common for
    // the OS versions.

    UNUSED(REF(shell));  // looked at via frame_ by OS_Create_Process
    UNUSED(REF(console));  // same

    char *os_input;
    REBCNT input_len;

    UNUSED(REF(input));  // implicit by void ARG(in)
    switch (VAL_TYPE(ARG(in))) {
    case REB_BLANK:
    case REB_MAX_NULLED:  // no /INPUT, so no argument provided
        os_input = NULL;
        input_len = 0;
        break;

    case REB_TEXT: {
        //
        // The interface allows passing a generic byte buffer, such that we
        // can send REB_BINARY (e.g. a CGI-style raw byte sequence to stdin,
        // or terminal escape sequences).  But if we're trying to send text,
        // Windows expects UCS2.
        //
      #ifdef OS_WIDE_CHAR
        unsigned int num_wchar = rebSpellIntoW(nullptr, 0, ARG(in));
        REBWCHAR *wchar_buf = rebAllocN(REBWCHAR, (num_wchar + 1) * 2);
        unsigned int check;
        check = rebSpellIntoW(wchar_buf, num_wchar, ARG(in));
        UNUSED(check);
        os_input = cast(char*, wchar_buf);
        input_len = num_wchar * 2;  // don't include wide '\0' char
      #else
        input_len = rebSpellInto(nullptr, 0, ARG(in));
        os_input = rebAllocN(char, input_len);
        size_t check;
        check = rebSpellInto(os_input, input_len, ARG(in));
        UNUSED(check);
      #endif
        break; }

    case REB_FILE: {
        size_t size;
        os_input = s_cast(rebBytes(  // !!! why fileNAME size passed in???
            &size,
            "file-to-local", ARG(in),
            rebEND
        ));
        input_len = size;
        break; }

    case REB_BINARY: {
        size_t size;
        os_input = s_cast(rebBytes(&size, ARG(in), rebEND));
        input_len = size;
        break; }

    default:
        panic(ARG(in));
    }

    UNUSED(REF(output));
    UNUSED(REF(error));

    bool flag_wait;
    if (
        REF(wait)
        or (
            IS_TEXT(ARG(in)) or IS_BINARY(ARG(in))
            or IS_TEXT(ARG(out)) or IS_BINARY(ARG(out))
            or IS_TEXT(ARG(err)) or IS_BINARY(ARG(err))
        ) // I/O redirection implies /WAIT
    ){
        flag_wait = true;
    }
    else
        flag_wait = false;

    // We synthesize the argc and argv from the "command", and in the process
    // we do dynamic allocations of argc strings through the API.  These need
    // to be freed before we return.
    //
    OSCHR *cmd;
    int argc;
    const OSCHR **argv;

    if (IS_TEXT(ARG(command))) {
        // `call {foo bar}` => execute %"foo bar"

        // !!! Interpreting string case as an invocation of %foo with argument
        // "bar" has been requested and seems more suitable.  Question is
        // whether it should go through the shell parsing to do so.

        cmd = rebValSpellingAllocOS(ARG(command));

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

        // !!! Make two copies because it frees cmd and all the argv.  Review.
        //
        argv[0] = rebValSpellingAllocOS(ARG(command));
        argv[1] = NULL;
    }
    else if (IS_BLOCK(ARG(command))) {
        // `call ["foo" "bar"]` => execute %foo with arg "bar"

        cmd = NULL;

        REBVAL *block = ARG(command);
        argc = VAL_LEN_AT(block);
        if (argc == 0)
            fail (Error_Too_Short_Raw());

        argv = rebAllocN(const OSCHR*, (argc + 1));

        int i;
        for (i = 0; i < argc; i ++) {
            RELVAL *param = VAL_ARRAY_AT_HEAD(block, i);
            if (IS_TEXT(param)) {
                argv[i] = rebValSpellingAllocOS(KNOWN(param));
            }
            else if (IS_FILE(param)) {
              #ifdef OS_WIDE_CHAR
                argv[i] = rebSpellW("file-to-local", KNOWN(param), rebEND);
              #else
                argv[i] = rebSpell("file-to-local", KNOWN(param), rebEND);
              #endif
            }
            else
                fail (Error_Bad_Value_Core(param, VAL_SPECIFIER(block)));
        }
        argv[argc] = NULL;
    }
    else if (IS_FILE(ARG(command))) {
        // `call %"foo bar"` => execute %"foo bar"

        cmd = NULL;

        argc = 1;
        argv = rebAllocN(const OSCHR*, (argc + 1));

      #ifdef OS_WIDE_CHAR
        argv[0] = rebSpellW("file-to-local", ARG(command), rebEND);
      #else
        argv[0] = rebSpell("file-to-local", ARG(command), rebEND);
      #endif

        argv[1] = NULL;
    }
    else
        fail (PAR(command));

    REBU64 pid;
    int exit_code;

    // If a STRING! or BINARY! is used for the output or error, then that
    // is treated as a request to append the results of the pipe to them.
    //
    // !!! At the moment this is done by having the OS-specific routine
    // pass back a buffer it malloc()s and reallocates to be the size of the
    // full data, which is then appended after the operation is finished.
    // With CALL now an extension where all parts have access to the internal
    // API, it could be added directly to the binary or string as it goes.

    // These are initialized to avoid a "possibly uninitialized" warning.
    //
    char *os_output = NULL;
    uint32_t output_len = 0;
    char *os_err = NULL;
    uint32_t err_len = 0;

    REBINT r = OS_Create_Process(
        frame_,
        cast(const OSCHR*, cmd),
        argc,
        cast(const OSCHR**, argv),
        flag_wait,
        &pid,
        &exit_code,
        os_input,
        input_len,
        IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)) ? &os_output : nullptr,
        IS_TEXT(ARG(out)) or IS_BINARY(ARG(out)) ? &output_len : nullptr,
        IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)) ? &os_err : nullptr,
        IS_TEXT(ARG(err)) or IS_BINARY(ARG(err)) ? &err_len : nullptr
    );

    // Call may not succeed if r != 0, but we still have to run cleanup
    // before reporting any error...

    assert(argc > 0);

    int i;
    for (i = 0; i != argc; ++i)
        rebFree(m_cast(OSCHR*, argv[i]));

    if (cmd != NULL)
        rebFree(cmd);

    rebFree(m_cast(OSCHR**, argv));

    if (IS_TEXT(ARG(out))) {
        if (output_len > 0) {
            Append_OS_Str(ARG(out), os_output, output_len);
            free(os_output);
        }
    }
    else if (IS_BINARY(ARG(out))) {
        if (output_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(ARG(out)), os_output, output_len);
            free(os_output);
        }
    }

    if (IS_TEXT(ARG(err))) {
        if (err_len > 0) {
            Append_OS_Str(ARG(err), os_err, err_len);
            free(os_err);
        }
    } else if (IS_BINARY(ARG(err))) {
        if (err_len > 0) {
            Append_Unencoded_Len(VAL_SERIES(ARG(err)), os_err, err_len);
            free(os_err);
        }
    }

    if (os_input != NULL)
        rebFree(os_input);

    if (REF(info)) {
        REBCTX *info = Alloc_Context(REB_OBJECT, 2);

        Init_Integer(Append_Context(info, NULL, Canon(SYM_ID)), pid);
        if (REF(wait))
            Init_Integer(
                Append_Context(info, NULL, Canon(SYM_EXIT_CODE)),
                exit_code
            );

        return Init_Object(D_OUT, info);
    }

    if (r != 0)
        rebFail_OS (r);

    // We may have waited even if they didn't ask us to explicitly, but
    // we only return a process ID if /WAIT was not explicitly used
    //
    if (REF(wait))
        return Init_Integer(D_OUT, exit_code);

    return Init_Integer(D_OUT, pid);
}


//
//  export get-os-browsers: native [
//
//  "Ask the OS or registry what command(s) to use for starting a browser."
//
//      return: [block!]
//          {Block of strings, where %1 should be substituted with the string}
//  ]
//
REBNATIVE(get_os_browsers)
//
// !!! Using the %1 convention is not necessarily ideal vs. having some kind
// of more "structural" result, it was just easy because it's how the string
// comes back from the Windows registry.  Review.
{
    PROCESS_INCLUDE_PARAMS_OF_GET_OS_BROWSERS;

    REBVAL *list = rebRun("copy []", rebEND);

  #if defined(TO_WINDOWS)

    HKEY key;
    if (
        RegOpenKeyEx(
            HKEY_CLASSES_ROOT,
            L"http\\shell\\open\\command",
            0,
            KEY_READ,
            &key
        ) != ERROR_SUCCESS
    ){
        fail ("Could not open registry key for http\\shell\\open\\command");
    }

    DWORD num_bytes = 0; // pass NULL and use 0 for initial length, to query

    DWORD type;
    DWORD flag = RegQueryValueExW(key, L"", 0, &type, NULL, &num_bytes);

    if (
        (flag != ERROR_MORE_DATA and flag != ERROR_SUCCESS)
        or num_bytes == 0
        or type != REG_SZ // RegQueryValueExW returns unicode
        or num_bytes % 2 != 0 // byte count should be even for unicode
    ){
        RegCloseKey(key);
        fail ("Could not read registry key for http\\shell\\open\\command");
    }

    REBCNT len = num_bytes / 2;

    WCHAR *buffer = rebAllocN(WCHAR, len + 1); // include terminator

    flag = RegQueryValueEx(
        key, L"", 0, &type, cast(LPBYTE, buffer), &num_bytes
    );
    RegCloseKey(key);

    if (flag != ERROR_SUCCESS)
        fail ("Could not read registry key for http\\shell\\open\\command");

    while (buffer[len - 1] == '\0') {
        //
        // Don't count terminators; seems the guarantees are a bit fuzzy
        // about whether the string in the registry has one included in the
        // byte count or not.
        //
        --len;
    }

    rebElide("append", list, rebR(rebLengthedTextW(buffer, len)), rebEND);

    rebFree(buffer);

  #elif defined(TO_LINUX)

    // Caller should try xdg-open first, then try x-www-browser otherwise
    //
    rebElide(
        "append", list, "[",
            rebT("xdg-open %1"),
            rebT("x-www-browser %1"),
        "]", rebEND
    );

  #else // Just try /usr/bin/open on POSIX, OS X, Haiku, etc.

    rebElide("append", list, rebT("/usr/bin/open %1"), rebEND);

  #endif

    return list;
}


//
//  export sleep: native [
//
//  "Use system sleep to wait a certain amount of time (doesn't use PORT!s)."
//
//      return: [void!]
//      duration [integer! decimal! time!]
//          {Length to sleep (integer and decimal are measuring seconds)}
//
//  ]
//
REBNATIVE(sleep)
//
// !!! This is a temporary workaround for the fact that it is not currently
// possible to do a WAIT on a time from within an AWAKE handler.  A proper
// solution would presumably solve that problem, so two different functions
// would not be needed.
//
// This function was needed by @GrahamChiu, and putting it in the CALL module
// isn't necessarily ideal, but it's better than making the core dependent
// on Sleep() vs. usleep()...and all the relevant includes have been
// established here.
{
    PROCESS_INCLUDE_PARAMS_OF_SLEEP;

    REBCNT msec = Milliseconds_From_Value(ARG(duration));

  #ifdef TO_WINDOWS
    Sleep(msec);
  #else
    usleep(msec * 1000);
  #endif

    return Init_Void(D_OUT);
}

#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
static void kill_process(pid_t pid, int signal);
#endif

//
//  terminate: native [
//
//  "Terminate a process (not current one)"
//
//      return: [<opt>]
//      pid [integer!]
//          {The process ID}
//  ]
//
REBNATIVE(terminate)
{
    PROCESS_INCLUDE_PARAMS_OF_TERMINATE;

  #ifdef TO_WINDOWS

    if (GetCurrentProcessId() == cast(DWORD, VAL_INT32(ARG(pid))))
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");

    DWORD err = 0;
    HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, VAL_INT32(ARG(pid)));
    if (ph == NULL) {
        err = GetLastError();
        switch (err) {
          case ERROR_ACCESS_DENIED:
            Fail_Permission_Denied();

          case ERROR_INVALID_PARAMETER:
            Fail_No_Process(ARG(pid));

          default:
            Fail_Terminate_Failed(err);
        }
    }

    if (TerminateProcess(ph, 0)) {
        CloseHandle(ph);
        return nullptr;
    }

    err = GetLastError();
    CloseHandle(ph);
    switch (err) {
      case ERROR_INVALID_HANDLE:
        Fail_No_Process(ARG(pid));

      default:
        Fail_Terminate_Failed(err);
    }

  #elif defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)

    if (getpid() == VAL_INT32(ARG(pid))) {
        // signal is not as reliable for this purpose
        // it's caught in host-main.c as to stop the evaluation
        fail ("Use QUIT or EXIT-REBOL to terminate current process, instead");
    }
    kill_process(VAL_INT32(ARG(pid)), SIGTERM);
    return nullptr;

  #else

    UNUSED(frame_);
    fail ("terminate is not implemented for this platform");

  #endif
}


//
//  export get-env: native [
//
//  {Returns the value of an OS environment variable (for current process).}
//
//      return: "String the variable was set to, or null if not set"
//          [<opt> text!]
//      variable "Name of variable to get (case-insensitive in Windows)"
//          [text! word!]
//  ]
//
REBNATIVE(get_env)
//
// !!! Prescriptively speaking, it is typically considered a bad idea to treat
// an empty string environment variable as different from an unset one:
//
// https://unix.stackexchange.com/q/27708/
//
// It might be worth it to require a refinement to treat empty strings in a
// different way, or to return them as BLANK! instead of plain TEXT! so they
// were falsey like nulls but might trigger awareness of their problematic
// nature in some string routines.  Review.
{
    PROCESS_INCLUDE_PARAMS_OF_GET_ENV;

    REBVAL *variable = ARG(variable);

    Check_Security(Canon(SYM_ENVR), POL_READ, variable);

    REBCTX *error = NULL;

  #ifdef TO_WINDOWS
    // Note: The Windows variant of this API is NOT case-sensitive

    WCHAR *key = rebSpellW(variable, rebEND);

    DWORD val_len_plus_one = GetEnvironmentVariable(key, NULL, 0);
    if (val_len_plus_one == 0) { // some failure...
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND)
            Init_Nulled(D_OUT);
        else
            error = Error_User("Unknown error when requesting variable size");
    }
    else {
        WCHAR *val = rebAllocN(WCHAR, val_len_plus_one);
        DWORD result = GetEnvironmentVariable(key, val, val_len_plus_one);
        if (result == 0)
            error = Error_User("Unknown error fetching variable to buffer");
        else {
            REBVAL *temp = rebLengthedTextW(val, val_len_plus_one - 1);
            Move_Value(D_OUT, temp);
            rebRelease(temp);
        }
        rebFree(val);
    }

    rebFree(key);
  #else
    // Note: The Posix variant of this API is case-sensitive

    char *key = rebSpell(variable, rebEND);

    const char* val = getenv(key);
    if (val == NULL) // key not present in environment
        Init_Nulled(D_OUT);
    else {
        size_t size = strsize(val);

        /* assert(size != 0); */ // True?  Should it return BLANK!?

        Init_Text(D_OUT, Make_Sized_String_UTF8(val, size));
    }

    rebFree(key);
  #endif

    // Error is broken out like this so that the proper freeing can be done
    // without leaking temporary buffers.
    //
    if (error != NULL)
        fail (error);

    return D_OUT;
}


//
//  export set-env: native [
//
//  {Sets value of operating system environment variable for current process.}
//
//      return: "Returns same value passed in"
//          [<opt> text!]
//      variable [<blank> text! word!]
//          "Variable to set (case-insensitive in Windows)"
//      value [<opt> text!]
//          "Value to set the variable to, or NULL to unset it"
//  ]
//
REBNATIVE(set_env)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_ENV;

    REBVAL *variable = ARG(variable);
    REBVAL *value = ARG(value);

    Check_Security(Canon(SYM_ENVR), POL_WRITE, variable);

  #ifdef TO_WINDOWS
    WCHAR *key_wide = rebSpellW(variable, rebEND);
    WCHAR *opt_val_wide = rebSpellW("ensure [<opt> text!]", value, rebEND);

    if (not SetEnvironmentVariable(key_wide, opt_val_wide)) // null unsets
        fail ("environment variable couldn't be modified");

    rebFree(opt_val_wide);
    rebFree(key_wide);
  #else
    char *key_utf8 = rebSpell(variable, rebEND);

    if (IS_NULLED(value)) {
      #ifdef unsetenv
        if (unsetenv(key_utf8) == -1)
            fail ("unsetenv() couldn't unset environment variable");
      #else
        // WARNING: KNOWN PORTABILITY ISSUE
        //
        // Simply saying putenv("FOO") will delete FOO from the environment,
        // but it's not consistent...does nothing on NetBSD for instance.  But
        // not all other systems have unsetenv...
        //
        // http://julipedia.meroh.net/2004/10/portability-unsetenvfoo-vs-putenvfoo.html
        //
        // going to hope this case doesn't hold onto the string...
        //
        if (putenv(key_utf8) == -1) // !!! Why mutable?
            fail ("putenv() couldn't unset environment variable");
      #endif
    }
    else {
      #ifdef setenv
        char *val_utf8 = rebSpell(value, rebEND);

        if (setenv(key_utf8, val_utf8, 1) == -1) // the 1 means "overwrite"
            fail ("setenv() coudln't set environment variable");

        rebFree(val_utf8);
      #else
        // WARNING: KNOWN MEMORY LEAK!
        //
        // putenv takes its argument as a single "key=val" string.  It is
        // *fatally flawed*, and obsoleted by setenv and unsetenv in System V:
        //
        // http://stackoverflow.com/a/5876818/211160
        //
        // Once you have passed a string to it you never know when that string
        // will no longer be needed.  Thus it may either not be dynamic or you
        // must leak it, or track a local copy of the environment yourself.
        //
        // If you're stuck without setenv on some old platform, but really
        // need to set an environment variable, here's a way that just leaks a
        // string each time you call.  The code would have to keep track of
        // each string added in some sort of a map...which is currently deemed
        // not worth the work.

        char *key_equals_val_utf8 = rebSpell(
            "unspaced [", variable, "{=}", value, "]", rebEND
        );

        if (putenv(key_equals_val_utf8) == -1) // !!! why mutable?  :-/
            fail ("putenv() couldn't set environment variable");

        /* rebFree(key_equals_val_utf8); */ // !!! Can't!  Crashes getenv()
        rebUnmanage(key_equals_val_utf8); // oh well, have to leak it
      #endif
    }

    rebFree(key_utf8);
  #endif

    RETURN (ARG(value));
}


//
//  export list-env: native [
//
//  {Returns a map of OS environment variables (for current process).}
//
//      ; No arguments
//  ]
//
REBNATIVE(list_env)
{
    PROCESS_INCLUDE_PARAMS_OF_LIST_ENV;

    REBVAL *map = rebRun("make map! []", rebEND);

  #ifdef TO_WINDOWS
    //
    // Windows environment strings are sequential null-terminated strings,
    // with a 0-length string signaling end ("keyA=valueA\0keyB=valueB\0\0")
    // We count the strings to know how big an array to make, and then
    // convert the array into a MAP!.
    //
    // !!! Adding to a map as we go along would probably be better.

    WCHAR *env = GetEnvironmentStrings();

    REBCNT len;
    const WCHAR *key_equals_val = env;
    while ((len = wcslen(key_equals_val)) != 0) {
        const WCHAR *eq_pos = wcschr(key_equals_val, '=');

        // "What are these strange =C: environment variables?"
        // https://blogs.msdn.microsoft.com/oldnewthing/20100506-00/?p=14133
        //
        if (eq_pos == key_equals_val) {
            key_equals_val += len + 1; // next
            continue;
        }

        int key_len = eq_pos - key_equals_val;
        REBVAL *key = rebLengthedTextW(key_equals_val, key_len);

        int val_len = len - (eq_pos - key_equals_val) - 1;
        REBVAL *val = rebLengthedTextW(eq_pos + 1, val_len);

        rebElide(
            "append", map, rebU("[", rebR(key), rebR(val), "]", rebEND),
        rebEND);

        key_equals_val += len + 1; // next
    }

    FreeEnvironmentStrings(env);
  #else
    // Note: 'environ' is an extern of a global found in <unistd.h>, and each
    // entry contains a `key=value` formatted string.
    //
    // https://stackoverflow.com/q/3473692/
    //
    int n;
    for (n = 0; environ[n] != NULL; ++n) {
        //
        // Note: it's safe to search for just a `=` byte, since the high bit
        // isn't set...and even if the key contains UTF-8 characters, there
        // won't be any occurrences of such bytes in multi-byte-characters.
        //
        const char *key_equals_val = environ[n];
        const char *eq_pos = strchr(key_equals_val, '=');

        REBCNT size = strlen(key_equals_val);

        int key_size = eq_pos - key_equals_val;
        REBVAL *key = rebSizedText(key_equals_val, key_size);

        int val_size = size - (eq_pos - key_equals_val) - 1;
        REBVAL *val = rebSizedText(eq_pos + 1, val_size);

        rebElide(
            "append", map, rebU("[", rebR(key), rebR(val), "]", rebEND),
        rebEND);
    }
  #endif

    return map;
}


#if defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)

//
//  get-pid: native [
//
//  "Get ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_pid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_PID;

    return rebInteger(getpid());
}



//
//  get-uid: native [
//
//  "Get real user ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getuid());
}


//
//  get-euid: native [
//
//  "Get effective user ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(geteuid());
}


//
//  get-gid: native [
//
//  "Get real group ID of the process"
//
//      return: [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_UID;

    return rebInteger(getgid());
}


//
//  get-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: [integer!]
//
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(get_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_GET_EUID;

    return rebInteger(getegid());
}


//
//  set-uid: native [
//
//  {Set real user ID of the process}
//
//      return: "Same ID as input"
//          [integer!]
//      uid {The effective user ID}
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_uid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_UID;

    if (setuid(VAL_INT32(ARG(uid))) >= 0)
        RETURN (ARG(uid));

    switch (errno) {
      case EINVAL:
        fail (PAR(uid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-euid: native [
//
//  {Get effective user ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      euid "The effective user ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_euid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EUID;

    if (seteuid(VAL_INT32(ARG(euid))) >= 0)
        RETURN (ARG(euid));

    switch (errno) {
      case EINVAL:
        fail (PAR(euid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-gid: native [
//
//  {Set real group ID of the process}
//
//      return: "Same ID as input"
//          [<opt>]
//      gid "The effective group ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_gid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_GID;

    if (setgid(VAL_INT32(ARG(gid))) >= 0)
        RETURN (ARG(gid));

    switch (errno) {
      case EINVAL:
        fail (PAR(gid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


//
//  set-egid: native [
//
//  "Get effective group ID of the process"
//
//      return: "Same ID as input"
//          [integer!]
//      egid "The effective group ID"
//          [integer!]
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(set_egid)
{
    PROCESS_INCLUDE_PARAMS_OF_SET_EGID;

    if (setegid(VAL_INT32(ARG(egid))) >= 0)
        RETURN (ARG(egid));

    switch (errno) {
      case EINVAL:
        fail (PAR(egid));

      case EPERM:
        Fail_Permission_Denied();

      default:
        rebFail_OS(errno);
    }
}


static void kill_process(pid_t pid, int signal)
{
    if (kill(pid, signal) >= 0)
        return; // success

    switch (errno) {
      case EINVAL:
        rebJumps(
            "fail [{Invalid signal number:}", rebI(signal), "]", rebEND
        );

      case EPERM:
        Fail_Permission_Denied();

      case ESRCH:
        Fail_No_Process(rebInteger(pid)); // failure releases integer handle

      default:
        rebFail_OS(errno);
    }
}


//
//  send-signal: native [
//
//  "Send signal to a process"
//
//      return: [void!] ;-- !!! might this return pid or signal (?)
//      pid [integer!]
//          {The process ID}
//      signal [integer!]
//          {The signal number}
//  ]
//  platforms: [linux android posix osx]
//
REBNATIVE(send_signal)
{
    PROCESS_INCLUDE_PARAMS_OF_SEND_SIGNAL;

    // !!! Is called `send-signal` but only seems to call kill (?)
    //
    kill_process(rebUnboxInteger(ARG(pid)), rebUnboxInteger(ARG(signal)));

    return Init_Void(D_OUT);
}

#endif // defined(TO_LINUX) || defined(TO_ANDROID) || defined(TO_POSIX) || defined(TO_OSX)
