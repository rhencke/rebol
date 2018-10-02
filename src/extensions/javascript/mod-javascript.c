//
//  File: %mod-javascript.c
//  Summary: "Support for calling Javascript from Rebol in Emscripten build"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Rebol Open Source Contributors
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
#include "sys-ext.h"

#include "tmp-mod-javascript-first.h"

#include <limits.h> // for UINT_MAX

#include <emscripten.h>

enum {
    IDX_NATIVE_SOURCE = 0, // text string source code of native (for SOURCE)
    IDX_NATIVE_CONTEXT = 1, // rebRun()/etc. bind here (and lib) when running
    IDX_NATIVE_HANDLE = 2, // handle gives hookpoint for GC of table entry
    IDX_NATIVE_MAX
}; // for the ACT_DETAILS() array of a javascript native


//
//  JavaScript_Dispatcher: C
//
// Called when the ACTION! produced by JS-NATIVE is run.
//
const REBVAL *JavaScript_Dispatcher(REBFRM *f)
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    UNUSED(details);

    // Getting a value back from JavaScript via EM_ASM_INT can give back an
    // unsigned int.  There are cases in the emscripten code where this is
    // presumed to be good enough to hold any heap address.  Do a sanity
    // check that we aren't truncating.
    //
    uintptr_t id = cast(uintptr_t, ACT_PARAMLIST(FRM_PHASE(f)));
    assert(id < UINT_MAX);

    uintptr_t r = EM_ASM_INT({
        return RL_Dispatch($0);
    }, id);

    return cast(const REBVAL*, r);
}


//
// cleanup_js_native: C
//
// GC-able HANDLE! avoids leaking in the table mapping integers (pointers) to
// JavaScript functions.
//
void cleanup_js_native(const REBVAL *v) {
    uintptr_t id = cast(uintptr_t, VAL_HANDLE_POINTER(REBARR*, v));
    assert(id < UINT_MAX);

    EM_ASM_({
        RL_Unregister($0);
    }, id);
}


//
//  js-native: native/export [
//
//  {Create an ACTION! whose body is a string of JavaScript code}
//
//      return: [action!]
//      spec [block!]
//          {Function specification (similar to the one used by FUNC)}
//      source [text!]
//          {JavaScript code as a text string}
//  ]
//
REBNATIVE(js_native)
{
    JAVASCRIPT_INCLUDE_PARAMS_OF_JS_NATIVE;

    REBVAL *source = ARG(source);

    if (VAL_LEN_AT(source) == 0)
        fail ("Source for JS-NATIVE can't be empty"); // auto return void?

    REBACT *native = Make_Action(
        Make_Paramlist_Managed_May_Fail(ARG(spec), MKF_MASK_NONE),
        &JavaScript_Dispatcher, // will be replaced e.g. by COMPILE
        nullptr, // no facade (use paramlist)
        nullptr, // no specialization exemplar (or inherited exemplar)
        IDX_NATIVE_MAX // details capacity [source module linkname tcc_state]
    );

    // When coding to the internal API, it's easy to make a mistake and call
    // into something that evaluates without having GC protection on array
    // elements when operating by index like this.  Pre-fill them.
    //
    REBARR *details = ACT_DETAILS(native);
    int idx;
    for (idx = 0; idx < IDX_NATIVE_MAX; ++idx)
        Init_Unreadable_Blank(ARR_AT(details, idx));
    TERM_ARRAY_LEN(details, IDX_NATIVE_MAX);

    if (Is_Series_Frozen(VAL_SERIES(source)))
        Move_Value(ARR_AT(details, IDX_NATIVE_SOURCE), source); // no copy
    else {
        Init_Text(
            ARR_AT(details, IDX_NATIVE_SOURCE),
            Copy_String_At_Len(source, -1) // might change
        );
    }

    // The generation of the function called by JavaScript.  It takes no
    // arguments, as giving it arguments would make calling it more complex
    // as well as introduce several issues regarding mapping legal Rebol
    // names to names for JavaScript parameters.  libRebol APIs must be used
    // to access the arguments out of the frame.

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    Append_Unencoded(mo->series, "RL_Register(");

    REBYTE buf[60]; // !!! Why 60?  Copied from MF_Integer()
    REBINT len = Emit_Integer(buf, cast(uintptr_t, ACT_PARAMLIST(native)));
    Append_Unencoded_Len(mo->series, s_cast(buf), len);

    Append_Unencoded(mo->series, ", function() {\n"); // would add ID number
    Append_Unencoded(mo->series, "return async function() {\n");

    // By not using `new function` we are able to make this an async function,
    // as well as avoid escaping of string literals.

    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(
        &offset,
        &size,
        source,
        VAL_LEN_AT(source)
    );
    Append_Utf8_Utf8(mo->series, cs_cast(BIN_AT(temp, offset)), size);

    Append_Unencoded(mo->series, "\n}\n"); // end `async function() {`
    Append_Unencoded(mo->series, "}()"); // invoke dummy function
    Append_Unencoded(mo->series, ");"); // end `RL_Register(`

    TERM_SERIES(mo->series);

    emscripten_run_script(cs_cast(BIN_AT(mo->series, mo->start)));

    Drop_Mold(mo);

    // !!! Natives on the stack can specify where APIs like rebRun() should
    // look for bindings.  For the moment, set user natives to use the user
    // context...it could be a parameter of some kind (?)
    //
    Move_Value(
        ARR_AT(details, IDX_NATIVE_CONTEXT),
        Get_System(SYS_CONTEXTS, CTX_USER)
    );

    Init_Handle_Managed(
        ARR_AT(details, IDX_NATIVE_HANDLE),
        ACT_PARAMLIST(native),
        0,
        &cleanup_js_native
    );

    SET_VAL_FLAGS(ACT_ARCHETYPE(native), ACTION_FLAG_NATIVE);
    return Init_Action_Unbound(D_OUT, native);
}


#include "tmp-mod-javascript-last.h"
