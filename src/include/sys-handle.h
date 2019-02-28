//
//  File: %sys-handle.h
//  Summary: "Definitions for GC-able and non-GC-able Handles"
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
// In Rebol terminology, a HANDLE! is a pointer to a function or data that
// represents an arbitrary external resource.  While such data could also
// be encoded as a BINARY! "blob" (as it might be in XML), the HANDLE! type
// is intentionally "opaque" to user code so that it is a black box.
//
// Additionally, Ren-C added the idea of a garbage collector callback for
// "Managed" handles.  This is implemented by means of making the handle cost
// a single REBSER node shared among its instances, which is a "singular"
// Array containing a canon value of the handle itself.  When there are no
// references left to the handle and the GC runs, it will run a hook stored
// in the ->misc field of the singular array.
//
// As an added benefit of the Managed form, the code and data pointers in the
// value itself are not used; instead preferring the data held in the REBARR.
// This allows one instance of a managed handle to have its code or data
// pointer changed and be reflected in all instances.  The simple form of
// handle however is such that each REBVAL copied instance is independent,
// and changing one won't change the others.
//
////=// NOTES /////////////////////////////////////////////////////////////=//
//
// * The ->extra field of the REBVAL may contain a singular REBARR that is
//   leveraged for its GC-awareness.  This leverages the GC-aware ability of a
//   REBSER to know when no references to the handle exist and call a cleanup
//   function.  The GC-aware variant allocates a "singular" array, which is
//   the exact size of a REBSER and carries the canon data.  If the cheaper
//   kind that's just raw data and no callback, ->extra is null.
//

inline static bool Is_Handle_Cfunc(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_HANDLE);
    return PAYLOAD(Handle, v).length == 0;
}

inline static uintptr_t VAL_HANDLE_LEN(const REBCEL *v) {
    assert(not Is_Handle_Cfunc(v));
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        return PAYLOAD(Handle, ARR_SINGLE(a)).length;
    else
        return PAYLOAD(Handle, v).length;
}

inline static void *VAL_HANDLE_VOID_POINTER(const REBCEL *v) {
    assert(not Is_Handle_Cfunc(v));
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        return PAYLOAD(Handle, ARR_SINGLE(a)).data.pointer;
    else
        return PAYLOAD(Handle, v).data.pointer;
}

#define VAL_HANDLE_POINTER(t, v) \
    cast(t *, VAL_HANDLE_VOID_POINTER(v))

inline static CFUNC *VAL_HANDLE_CFUNC(const REBCEL *v) {
    assert(Is_Handle_Cfunc(v));
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        return PAYLOAD(Handle, ARR_SINGLE(a)).data.cfunc;
    else
        return PAYLOAD(Handle, v).data.cfunc;
}

inline static CLEANUP_CFUNC *VAL_HANDLE_CLEANER(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_HANDLE);
    REBARR *a = EXTRA(Handle, v).singular;
    if (not a)
        return nullptr;
    return MISC(a).cleaner;
}

inline static void SET_HANDLE_LEN(REBCEL *v, uintptr_t length) {
    assert(CELL_KIND(v) == REB_HANDLE);
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        PAYLOAD(Handle, ARR_SINGLE(a)).length = length;
    else
        PAYLOAD(Handle, v).length = length;
}

inline static void SET_HANDLE_POINTER(REBCEL *v, void *pointer) {
    assert(not Is_Handle_Cfunc(v));
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        PAYLOAD(Handle, ARR_SINGLE(a)).data.pointer = pointer;
    else
        PAYLOAD(Handle, v).data.pointer = pointer;
}

inline static void SET_HANDLE_CFUNC(REBCEL *v, CFUNC *cfunc) {
    assert(Is_Handle_Cfunc(v));
    REBARR *a = EXTRA(Handle, v).singular;
    if (a)
        PAYLOAD(Handle, ARR_SINGLE(a)).data.cfunc = cfunc;
    else
        PAYLOAD(Handle, v).data.cfunc = cfunc;
}

inline static REBVAL *Init_Handle_Simple(
    RELVAL *out,
    void *pointer,
    uintptr_t length
){
    assert(length != 0); // can't be 0 unless cfunc (see also malloc(0))
    RESET_CELL(out, REB_HANDLE, CELL_MASK_NONE);
    EXTRA(Handle, out).singular = nullptr;
    PAYLOAD(Handle, out).data.pointer = pointer;
    PAYLOAD(Handle, out).length = length;
    return KNOWN(out);
}

inline static REBVAL *Init_Handle_Cfunc(
    RELVAL *out,
    CFUNC *cfunc
){
    RESET_CELL(out, REB_HANDLE, CELL_MASK_NONE);
    EXTRA(Handle, out).singular = nullptr;
    PAYLOAD(Handle, out).data.cfunc = cfunc;
    PAYLOAD(Handle, out).length = 0; // signals cfunc
    return KNOWN(out);
}

inline static void Init_Handle_Managed_Common(
    RELVAL *out,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    REBARR *singular = Alloc_Singular(NODE_FLAG_MANAGED);
    MISC(singular).cleaner = cleaner;

    RELVAL *v = ARR_SINGLE(singular);
    EXTRA(Handle, v).singular = singular; 
    PAYLOAD(Handle, v).length = length;

    // Caller will fill in whichever field is needed.  Note these are both
    // the same union member, so trashing them both is semi-superfluous, but
    // serves a commentary purpose here.
    //
    TRASH_POINTER_IF_DEBUG(PAYLOAD(Handle, v).data.pointer);
    TRASH_CFUNC_IF_DEBUG(PAYLOAD(Handle, v).data.cfunc);

    // Don't fill the handle properties in the instance if it's the managed
    // form.  This way, you can set the properties in the canon value and
    // effectively update all instances...since the bits live in the shared
    // series component.
    //
    RESET_CELL(out, REB_HANDLE, CELL_MASK_NONE);
    EXTRA(Handle, out).singular = singular;
    TRASH_POINTER_IF_DEBUG(PAYLOAD(Handle, out).data.pointer);
}

inline static REBVAL *Init_Handle_Managed(
    RELVAL *out,
    void *pointer,
    uintptr_t length,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, length, cleaner);

    // Leave the non-singular cfunc as trash; clients should not be using
    //
    RESET_VAL_HEADER(out, REB_HANDLE, CELL_MASK_NONE);

    REBARR *a = EXTRA(Handle, out).singular;
    RESET_VAL_HEADER(ARR_SINGLE(a), REB_HANDLE, CELL_MASK_NONE);
    PAYLOAD(Handle, ARR_SINGLE(a)).data.pointer = pointer;
    return KNOWN(out);
}

inline static REBVAL *Init_Handle_Managed_Cfunc(
    RELVAL *out,
    CFUNC *cfunc,
    CLEANUP_CFUNC *cleaner
){
    Init_Handle_Managed_Common(out, 0, cleaner);

    // Leave the non-singular cfunc as trash; clients should not be using
    //
    RESET_VAL_HEADER(out, REB_HANDLE, CELL_MASK_NONE);
    
    REBARR *a = EXTRA(Handle, out).singular;
    RESET_VAL_HEADER(ARR_HEAD(a), REB_HANDLE, CELL_MASK_NONE);
    PAYLOAD(Handle, ARR_HEAD(a)).data.cfunc = cfunc;
    PAYLOAD(Handle, ARR_HEAD(a)).length = 0;
    return KNOWN(out);
}
