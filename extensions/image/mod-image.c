//
//  File: %mod-image.c
//  Summary: "IMAGE! extension main C file"
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
//=////////////////////////////////////////////////////////////////////////=//
//
// See notes in %extensions/image/README.md

#include "sys-core.h"

#include "tmp-mod-image.h"

#include "sys-image.h"


//
//  register-image-hooks: native [
//
//  {Make the IMAGE! datatype work with GENERIC actions, comparison ops, etc}
//
//      return: [void!]
//  ]
//
REBNATIVE(register_image_hooks)
{
    IMAGE_INCLUDE_PARAMS_OF_REGISTER_IMAGE_HOOKS;

    // !!! See notes on Hook_Datatype for this poor-man's substitute for a
    // coherent design of an extensible object system (as per Lisp's CLOS)
    //
    Hook_Datatype(
        REB_IMAGE,
        &T_Image,
        &PD_Image,
        &CT_Image,
        &MAKE_Image,
        &TO_Image,
        &MF_Image
    );

    return Init_Void(D_OUT);
}


//
//  unregister-image-hooks: native [
//
//  {Remove behaviors for IMAGE! added by REGISTER-IMAGE-HOOKS}
//
//      return: [void!]
//  ]
//
REBNATIVE(unregister_image_hooks)
{
    IMAGE_INCLUDE_PARAMS_OF_UNREGISTER_IMAGE_HOOKS;

    Unhook_Datatype(REB_IMAGE);

    return Init_Void(D_OUT);
}
