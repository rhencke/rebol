//
//  File: %host-process.c
//  Summary: "POSIX Process API"
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
// This was originally the file host-lib.c, providing the entire
// host API.  When the host routines were broken into smaller
// pieces, it made sense that host-lib.c be kept as the largest
// set of related routines.  That turned out to be the process
// related routines and support for CALL.
//

#if !defined( __cplusplus) && defined(TO_LINUX)
    // See feature_test_macros(7)
    // This definition is redundant under C++
    #define _GNU_SOURCE  // Needed for pipe2 on Linux
#endif

#include <errno.h>
#include <stdlib.h>
#include <sys/wait.h>

#include <assert.h>

#if !defined(NDEBUG)
    #include <stdio.h>
#endif

#include "reb-host.h"


/***********************************************************************
**
**  OS Library Functions
**
***********************************************************************/


//
//  OS_Reap_Process: C
//
// pid:
//     > 0, a signle process
//     -1, any child process
//
// flags:
//     0: return immediately
//
// Return -1 on error
//
int OS_Reap_Process(int pid, int *status, int flags)
{
    return waitpid(pid, status, flags == 0? WNOHANG : 0);
}
