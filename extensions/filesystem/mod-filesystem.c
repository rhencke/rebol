//
//  File: %mod-filesystem.c
//  Summary: "POSIX/Windows File and Directory Access"
//  Section: ports
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

#include "tmp-mod-filesystem.h"

extern REB_R File_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb);
extern REB_R Dir_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb);


//
//  export get-file-actor-handle: native [
//
//  {Retrieve handle to the native actor for files}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_file_actor_handle)
{
    OS_REGISTER_DEVICE(&Dev_File);

    Make_Port_Actor_Handle(D_OUT, &File_Actor);
    return D_OUT;
}


//
//  get-dir-actor-handle: native [
//
//  {Retrieve handle to the native actor for directories}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_dir_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Dir_Actor);
    return D_OUT;
}
