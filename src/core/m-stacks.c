//
//  File: %m-stack.c
//  Summary: "data and function call stack implementation"
//  Section: memory
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


//
//  Startup_Stacks: C
//
void Startup_Stacks(REBCNT size)
{
    // We always keep one chunker around for the first chunk push, and prep
    // one chunk so that the push and drop routines never worry about testing
    // for the empty case.

    TG_Root_Chunker = cast(
        struct Reb_Chunker*,
        Alloc_Mem(CS_CHUNKER_MIN_LEN * sizeof(REBVAL))
    );

    Prep_Stack_Cell(&TG_Root_Chunker->info);
    SET_END(&TG_Root_Chunker->info); // "REB_0_CHUNKER"
    CHUNKER_NEXT(TG_Root_Chunker) = nullptr;
    CHUNKER_AVAIL(TG_Root_Chunker) = CS_CHUNKER_MIN_LEN - 1;

    REBCNT n;
    for (n = 0; n < CS_CHUNKER_MIN_LEN - 1; ++n)
        Prep_Stack_Cell(&TG_Root_Chunker->values[n]);

    TG_Top_Chunk = cast(struct Reb_Chunk*, &TG_Root_Chunker->values);
    CHUNK_PREV(TG_Top_Chunk) = nullptr;

    // Zero values for initial chunk, also sets offset to 0
    //
    SET_END(&TG_Top_Chunk->subinfo); // "REB_0_CHUNK"
    CHUNK_CHUNKER(TG_Top_Chunk) = TG_Root_Chunker;
    CHUNK_LEN(TG_Top_Chunk) = 0;
    SET_END(&TG_Top_Chunk->subvalues[0]);
    CHUNKER_AVAIL(TG_Root_Chunker) -= 1;

  #if !defined(NDEBUG)
    TG_Top_Chunk->subinfo.header.bits |= CELL_FLAG_PROTECTED;
    TG_Top_Chunk->subvalues[0].header.bits |= CELL_FLAG_PROTECTED;
  #endif

    // Start the data stack out with just one element in it, and make it an
    // unreadable blank in the debug build.  This helps avoid accidental
    // reads and is easy to notice when it is overwritten.  It also means
    // that indices into the data stack can be unsigned (no need for -1 to
    // mean empty, because 0 can)
    //
    // DS_PUSH checks what you're pushing isn't void, as most arrays can't
    // contain them.  But DS_PUSH_MAYBE_VOID allows you to, in case you
    // are building a context varlist or similar.
    //
    DS_Array = Make_Array_Core(1, ARRAY_FLAG_NULLEDS_LEGAL);
    Init_Unreadable_Blank(ARR_HEAD(DS_Array));

    // The END marker will signal DS_PUSH that it has run out of space,
    // and it will perform the allocation at that time.
    //
    TERM_ARRAY_LEN(DS_Array, 1);
    ASSERT_ARRAY(DS_Array);

    // Reuse the expansion logic that happens on a DS_PUSH to get the
    // initial stack size.  It requires you to be on an END to run.
    //
    DS_Index = 1;
    DS_Movable_Top = KNOWN(ARR_AT(DS_Array, DS_Index)); // can't push RELVALs
    Expand_Data_Stack_May_Fail(size);

    // Now drop the hypothetical thing pushed that triggered the expand.
    //
    DS_DROP;

    // Call stack (includes pending functions, parens...anything that sets
    // up a `REBFRM` and calls Do_Core())  Singly linked.
    //
    TG_Frame_Stack = NULL;
}


//
//  Shutdown_Stacks: C
//
void Shutdown_Stacks(void)
{
    assert(FS_TOP == NULL);
    assert(DSP == 0);
    ASSERT_UNREADABLE_IF_DEBUG(ARR_HEAD(DS_Array));

    Free_Unmanaged_Array(DS_Array);

    assert(TG_Top_Chunk == cast(struct Reb_Chunk*, &TG_Root_Chunker->values));

    // Because we always keep one chunker of headroom allocated, and the
    // push/drop is not designed to manage the last chunk, we *might* have
    // that next chunk of headroom still allocated.
    //
    struct Reb_Chunker *next = CHUNKER_NEXT(TG_Root_Chunker);
    if (next)
        Free_Mem(next, (CHUNKER_AVAIL(next) + 1) * sizeof(REBVAL));

    // OTOH we always have to free the root chunker.
    //
    CHUNKER_AVAIL(TG_Root_Chunker) += 1; // drop empty top chunk
    Free_Mem(
        TG_Root_Chunker,
        (CHUNKER_AVAIL(TG_Root_Chunker) + 1) * sizeof(REBVAL)
    );
}


//
//  Expand_Data_Stack_May_Fail: C
//
// The data stack maintains an invariant that you may never push an END to it.
// So each push looks to see if it's pushing to a cell that contains an END
// and if so requests an expansion.
//
// WARNING: This will invalidate any extant pointers to REBVALs living in
// the stack.  It is for this reason that stack access should be done by
// REBDSP "data stack pointers" and not by REBVAL* across *any* operation
// which could do a push or pop.  (Currently stable w.r.t. pop but there may
// be compaction at some point.)
//
void Expand_Data_Stack_May_Fail(REBCNT amount)
{
    REBCNT len_old = ARR_LEN(DS_Array);

    // The current requests for expansion should only happen when the stack
    // is at its end.  Sanity check that.
    //
    assert(len_old == DS_Index);
    assert(IS_END(DS_Movable_Top));
    assert(DS_Movable_Top == KNOWN(ARR_TAIL(DS_Array)));
    assert(DS_Movable_Top - KNOWN(ARR_HEAD(DS_Array)) == cast(int, len_old));

    // If adding in the requested amount would overflow the stack limit, then
    // give a data stack overflow error.
    //
    if (SER_REST(SER(DS_Array)) + amount >= STACK_LIMIT) {
        //
        // Because the stack pointer was incremented and hit the END marker
        // before the expansion, we have to decrement it if failing.
        //
        --DS_Index;
        Fail_Stack_Overflow(); // !!! Should this be a "data stack" message?
    }

    Extend_Series(SER(DS_Array), amount);

    // Update the pointer used for fast access to the top of the stack that
    // likely was moved by the above allocation (needed before using DS_TOP)
    //
    DS_Movable_Top = cast(REBVAL*, ARR_AT(DS_Array, DS_Index));

    // We fill in the data stack with "GC safe trash" (which is void in the
    // release build, but will raise an alarm if VAL_TYPE() called on it in
    // the debug build).  In order to serve as a marker for the stack slot
    // being available, it merely must not be IS_END()...

    REBVAL *value = DS_Movable_Top;

    REBCNT len_new = len_old + amount;
    REBCNT n;
    for (n = len_old; n < len_new; ++n) {
        Init_Unreadable_Blank(value);
        ++value;
    }

    // Update the end marker to serve as the indicator for when the next
    // stack push would need to expand.
    //
    TERM_ARRAY_LEN(DS_Array, len_new);
    assert(value == ARR_TAIL(DS_Array));

    ASSERT_ARRAY(DS_Array);
}


//
//  Pop_Stack_Values_Core: C
//
// Pops computed values from the stack to make a new ARRAY.
//
REBARR *Pop_Stack_Values_Core(REBDSP dsp_start, REBFLGS flags)
{
    REBARR *array = Copy_Values_Len_Shallow_Core(
        DS_AT(dsp_start + 1), // start somewhere in the stack, end at DS_TOP
        SPECIFIED, // data stack should be fully specified--no relative values
        DSP - dsp_start, // len
        flags
    );

    DS_DROP_TO(dsp_start);
    return array;
}


//
//  Pop_Stack_Values_Into: C
//
// Pops computed values from the stack into an existing ANY-ARRAY.  The
// index of that array will be updated to the insertion tail (/INTO protocol)
//
void Pop_Stack_Values_Into(REBVAL *into, REBDSP dsp_start) {
    REBCNT len = DSP - dsp_start;
    REBVAL *values = KNOWN(ARR_AT(DS_Array, dsp_start + 1));

    assert(ANY_ARRAY(into));
    FAIL_IF_READ_ONLY_ARRAY(VAL_ARRAY(into));

    VAL_INDEX(into) = Insert_Series(
        SER(VAL_ARRAY(into)),
        VAL_INDEX(into),
        cast(REBYTE*, values), // stack only holds fully specified REBVALs
        len // multiplied by width (sizeof(REBVAL)) in Insert_Series
    );

    DS_DROP_TO(dsp_start);
}


//
//  Context_For_Frame_May_Reify_Managed: C
//
// A Reb_Frame does not allocate a series node for it to be used in FRAME!
// any_context payloads by default.  Operations like `context of 'return` will
// allocate them.  The debugger can even make them for native functions for
// stack introspection (though they should be read-only for user access).
//
// !!! Techniques are planned to allow REBVAL* with any_context payloads that
// point directly at REBFRM* with no series node the GC has to process.  This
// would only be possible if the cell storing its lifetime is not longer than
// the lifetime of the REBFRM*.  (This is similar to how REBFRM* are permitted
// in "direct binding" slots.)
//
// If there's already a frame this returns it, otherwise create it.  The
// managed status is so it's safe to refer to from FRAME! REBVAL*, since the
// arguments and locals in the frame's contents are marked by the frame stack.
//
REBCTX *Context_For_Frame_May_Reify_Managed(REBFRM *f)
{
    assert(Is_Action_Frame(f));
    if (f->reified != GHOST)
        return f->reified;

    REBARR *varlist = Alloc_Singular(
        ARRAY_FLAG_VARLIST | SERIES_FLAG_STACK | NODE_FLAG_MANAGED
    );

    // Changing the values in a running native's frame out from under it can
    // cause the interpreter to crash.  Mark the varlist array as being
    // "locked due to running", which won't stop FRM_ARG() from writing to
    // arguments in the native itself, but stops modifications via user code.
    //
    if (f->flags.bits & DO_FLAG_NATIVE_HOLD)
        SET_SER_INFO(varlist, SERIES_INFO_HOLD);

    if (Is_Action_Frame_Fulfilling(f)) {
        //
        // Generally only the debugger should be able to reify a function
        // frame that is still fulfilling.  (TBD: Enforce this?)  However it
        // needs to be possible, because the userspace debugger has to have
        // some way of modeling the in-progress stack, and it doesn't make
        // much sense to do this with anything but a FRAME! value.  But a
        // frame in such a state must not permit access to fields.
        //
        SET_SER_INFO(varlist, SERIES_INFO_INACCESSIBLE);
    }

    MISC(varlist).meta = nullptr; // seen by GC, must initialize
    LINK(varlist).keysource = NOD(f); // see notes on LINK().keysource

    REBVAL *rootvar = SINK(ARR_SINGLE(varlist));
    RESET_VAL_HEADER(rootvar, REB_FRAME);
    rootvar->payload.any_context.varlist = varlist;
    rootvar->payload.any_context.phase = f->phase;

    // The binding on the rootvar is important...this is how Get_Var_Core()
    // can know what the binding in the ACTION! value that spawned the
    // frame, even after the frame is expired.
    //
    INIT_BINDING(rootvar, f->binding);

    REBCTX *c = CTX(varlist);
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(c));
    ASSERT_CONTEXT(c);
    f->reified = c;
    return c;
}
