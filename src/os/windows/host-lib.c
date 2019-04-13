//
//  File: %host-lib.c
//  Summary: {OS API function library called by REBOL interpreter}
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
// This module is parsed for function declarations used to
// build prototypes, tables, and other definitions. To change
// function arguments requires a rebuild of the REBOL library.
//
// This module provides the functions that REBOL calls
// to interface to the native (host) operating system.
// REBOL accesses these functions through the structure
// defined in host-lib.h (auto-generated, do not modify).
//
// compile with -DUNICODE for Win32 wide char API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// WARNING: The function declarations here cannot be modified without also
// modifying those found in the other OS host-lib files!  Do not even modify
// the argument names.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <process.h>
#include <assert.h>

#include "reb-host.h"


//
//  Convert_Date: C
//
// Convert local format of system time into standard date
// and time structure.
//
// !!! The OS_XXX APIs were not intended to pass Windows datatypes.  As an
// interim step in phasing this API layer out, it needs to be able to be
// used by the Windows version of the FILESYSTEM extension--as well as code
// here.  So it takes a void pointer to a system time.
//
REBVAL *OS_Convert_Date(const void *systemtime, long zone)
{
    const SYSTEMTIME *stime = cast(const SYSTEMTIME*, systemtime);

    return rebValue("ensure date! (make-date-ymdsnz",
        rebI(stime->wYear), // year
        rebI(stime->wMonth), // month
        rebI(stime->wDay), // day
        rebI(
            stime->wHour * 3600 + stime->wMinute * 60 + stime->wSecond
        ), // "secs"
        rebI(1000000 * stime->wMilliseconds), // nano
        rebI(zone), // zone
    ")", rebEND);
}


/***********************************************************************
**
**  OS Library Functions
**
***********************************************************************/


//
//  OS_Get_Current_Dir: C
//
// Return the current directory path as a FILE!.  Result should be freed
// with rebRelease()
//
REBVAL *OS_Get_Current_Dir(void)
{
    DWORD len = GetCurrentDirectory(0, NULL); // length, incl terminator.
    WCHAR *path = rebAllocN(WCHAR, len);
    GetCurrentDirectory(len, path);

    REBVAL *result = rebValue(
        "local-to-file/dir", rebR(rebTextWide(path)),
    rebEND);
    rebFree(path);
    return result;
}


//
//  OS_Set_Current_Dir: C
//
// Set the current directory to local path.  Return false on failure.
//
bool OS_Set_Current_Dir(const REBVAL *path)
{
    WCHAR *path_wide = rebSpellWide("file-to-local/full", path, rebEND);

    BOOL success = SetCurrentDirectory(path_wide);

    rebFree(path_wide);

    return success == TRUE;
}


//
//  OS_Open_Library: C
//
// Load a DLL library and return the handle to it.
// If zero is returned, error indicates the reason.
//
void *OS_Open_Library(const REBVAL *path)
{
    // While often when communicating with the OS, the local path should be
    // fully resolved, the LoadLibraryW() function searches DLL directories by
    // default.  So if %foo is passed in, you don't want to prepend the
    // current dir to make it absolute, because it will *only* look there.
    //
    WCHAR *path_utf8 = rebSpellWide("file-to-local", path, rebEND);

    void *dll = LoadLibraryW(path_utf8);

    rebFree(path_utf8);

    if (not dll)
        rebFail_OS (GetLastError());

    return dll;
}


//
//  OS_Close_Library: C
//
// Free a DLL library opened earlier.
//
void OS_Close_Library(void *dll)
{
    FreeLibrary((HINSTANCE)dll);
}


//
//  OS_Find_Function: C
//
// Get a DLL function address from its string name.
//
CFUNC *OS_Find_Function(void *dll, const char *funcname)
{
    // !!! See notes about data pointers vs. function pointers in the
    // definition of CFUNC.  This is trying to stay on the right side
    // of the specification, but OS APIs often are not standard C.  So
    // this implementation is not guaranteed to work, just to suppress
    // compiler warnings.  See:
    //
    //      http://stackoverflow.com/a/1096349/211160

    FARPROC fp = GetProcAddress((HMODULE)dll, funcname);

    //DWORD err = GetLastError();

    return cast(CFUNC*, fp);
}


//
//  OS_Get_Current_Exec: C
//
// Return the current executable path as a FILE!.  The result should be freed
// with rebRelease()
//
REBVAL *OS_Get_Current_Exec(void)
{
    WCHAR *path = rebAllocN(WCHAR, MAX_PATH);

    DWORD r = GetModuleFileName(NULL, path, MAX_PATH);
    if (r == 0) {
        rebFree(path);
        return rebBlank();
    }
    path[r] = '\0'; // May not be NULL-terminated if buffer is not big enough

    REBVAL *result = rebValue(
        "local-to-file", rebR(rebTextWide(path)),
    rebEND);
    rebFree(path);

    return result;
}
