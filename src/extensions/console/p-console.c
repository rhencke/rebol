//
//  File: %p-console.c
//  Summary: "console port interface"
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


#define OUT_BUF_SIZE 32*1024

//
//  Console_Actor: C
//
REB_R Console_Actor(REBFRM *frame_, REBVAL *port, REBVAL *verb)
{
    REBCTX *ctx = VAL_CONTEXT(port);

    REBREQ *req = Ensure_Port_State(port, RDI_STDIO);

    switch (VAL_WORD_SYM(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // implied by `port`
        REBSYM property = VAL_WORD_SYM(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_OPEN_Q:
            return Init_Logic(D_OUT, did (Req(req)->flags & RRF_OPEN));

        default:
            break;
        }

        break; }

    case SYM_READ: {
        INCLUDE_PARAMS_OF_READ;

        UNUSED(PAR(source));

        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(seek)) {
            UNUSED(ARG(index));
            fail (Error_Bad_Refines_Raw());
        }
        UNUSED(PAR(string)); // handled in dispatcher
        UNUSED(PAR(lines)); // handled in dispatcher

        // If not open, open it:
        if (not (Req(req)->flags & RRF_OPEN))
            OS_DO_DEVICE_SYNC(req, RDC_OPEN);

        // If no buffer, create a buffer:
        //
        REBVAL *data = CTX_VAR(ctx, STD_PORT_DATA);
        if (not IS_BINARY(data))
            Init_Binary(data, Make_Binary(OUT_BUF_SIZE));

        REBSER *ser = VAL_SERIES(data);
        SET_SERIES_LEN(ser, 0);
        TERM_SERIES(ser);

        Req(req)->common.data = BIN_HEAD(ser);
        Req(req)->length = SER_AVAIL(ser);

        OS_DO_DEVICE_SYNC(req, RDC_READ);

        // !!! Among many confusions in this file, it said "Another copy???"
        //
        return Init_Binary(
            D_OUT,
            Copy_Bytes(Req(req)->common.data, Req(req)->actual)
        ); }

    case SYM_OPEN: {
        Req(req)->flags |= RRF_OPEN;
        RETURN (port); }

    case SYM_CLOSE:
        Req(req)->flags &= ~RRF_OPEN;
        //OS_DO_DEVICE(req, RDC_CLOSE);
        RETURN (port);

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_PORT, verb));
}
