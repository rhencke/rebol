//
//  File: %f-modify.c
//  Summary: "block series modification (insert, append, change)"
//  Section: functional
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


//
//  Modify_Array: C
//
// Returns new dst_idx
//
REBCNT Modify_Array(
    REBSTR *verb,           // INSERT, APPEND, CHANGE
    REBARR *dst_arr,        // target
    REBCNT dst_idx,         // position
    const REBVAL *src_val,  // source
    REBCNT flags,           // AM_SPLICE, AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBCNT tail = ARR_LEN(dst_arr);

    const RELVAL *src_rel;
    REBSPC *specifier;

    if (IS_NULLED(src_val) and sym == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty block.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_BLOCK;
    }

    if (IS_NULLED(src_val) or dups <= 0) {
        // If they are effectively asking for "no action" then all we have
        // to do is return the natural index result for the operation.
        // (APPEND will return 0, insert the tail of the insertion...so index)

        return (sym == SYM_APPEND) ? 0 : dst_idx;
    }

    if (sym == SYM_APPEND or dst_idx > tail)
        dst_idx = tail;

    // Each dup being inserted need a newline signal after it if:
    //
    // * The user explicitly invokes the /LINE refinement (AM_LINE flag)
    // * It's a spliced insertion and there's a NEWLINE_BEFORE flag on the
    //   element *after* the last item in the dup
    // * It's a spliced insertion and there dup goes to the end of the array
    //   so there's no element after the last item, but NEWLINE_AT_TAIL is set
    //   on the inserted array.
    //
    bool tail_newline = did (flags & AM_LINE);
    REBINT ilen;

    // Check /PART, compute LEN:
    if (flags & AM_SPLICE) {
        const REBCEL *unescaped = VAL_UNESCAPED(src_val);
        assert(ANY_ARRAY_KIND(CELL_KIND(unescaped)));

        // Adjust length of insertion if changing /PART:
        if (sym != SYM_CHANGE and (flags & AM_PART))
            ilen = dst_len;
        else
            ilen = VAL_LEN_AT(unescaped);

        if (not tail_newline) {
            RELVAL *tail_cell = VAL_ARRAY_AT(unescaped) + ilen;
            if (IS_END(tail_cell)) {
                tail_newline = GET_ARRAY_FLAG(
                    VAL_ARRAY(src_val),
                    NEWLINE_AT_TAIL
                );
            }
            else if (ilen == 0)
                tail_newline = false;
            else
                tail_newline = GET_CELL_FLAG(tail_cell, NEWLINE_BEFORE);
        }

        // Are we modifying ourselves? If so, copy src_val block first:
        if (dst_arr == VAL_ARRAY(unescaped)) {
            REBARR *copy = Copy_Array_At_Extra_Shallow(
                VAL_ARRAY(unescaped),
                VAL_INDEX(unescaped),
                VAL_SPECIFIER(unescaped),
                0, // extra
                NODE_FLAG_MANAGED // !!! Worth it to not manage and free?
            );
            src_rel = ARR_HEAD(copy);
            specifier = SPECIFIED; // copy already specified it
        }
        else {
            src_rel = VAL_ARRAY_AT(unescaped); // skips by VAL_INDEX values
            specifier = VAL_SPECIFIER(unescaped);
        }
    }
    else {
        // use passed in RELVAL and specifier
        ilen = 1;
        src_rel = src_val;
        specifier = SPECIFIED; // it's a REBVAL, not a RELVAL, so specified
    }

    REBINT size = dups * ilen; // total to insert

    // If data is being tacked onto an array, beyond the newlines on the values
    // in that array there is also the chance that there's a newline tail flag
    // on the target, and the insertion is at the end.
    //
    bool head_newline =
        (dst_idx == ARR_LEN(dst_arr))
        and GET_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);

    if (sym != SYM_CHANGE) {
        // Always expand dst_arr for INSERT and APPEND actions:
        Expand_Series(SER(dst_arr), dst_idx, size);
    }
    else {
        if (size > dst_len)
            Expand_Series(SER(dst_arr), dst_idx, size - dst_len);
        else if (size < dst_len and (flags & AM_PART))
            Remove_Series_Units(SER(dst_arr), dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(SER(dst_arr), size - (tail - dst_idx));
        }
    }

    tail = (sym == SYM_APPEND) ? 0 : size + dst_idx;

    REBINT dup_index = 0;
    for (; dup_index < dups; ++dup_index) {
        REBINT index = 0;
        for (; index < ilen; ++index, ++dst_idx) {
            Derelativize(
                ARR_HEAD(dst_arr) + dst_idx,
                src_rel + index,
                specifier
            );

            if (dup_index == 0 and index == 0 and head_newline) {
                SET_CELL_FLAG(ARR_HEAD(dst_arr) + dst_idx, NEWLINE_BEFORE);

                // The array flag is not cleared until the loop actually
                // makes a value that will carry on the bit.
                //
                CLEAR_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);
                continue;
            }

            if (dup_index > 0 and index == 0 and tail_newline) {
                SET_CELL_FLAG(ARR_HEAD(dst_arr) + dst_idx, NEWLINE_BEFORE);
            }
        }
    }

    // The above loop only puts on (dups - 1) NEWLINE_BEFORE flags.  The
    // last one might have to be the array flag if at tail.
    //
    if (tail_newline) {
        if (dst_idx == ARR_LEN(dst_arr))
            SET_ARRAY_FLAG(dst_arr, NEWLINE_AT_TAIL);
        else
            SET_CELL_FLAG(ARR_AT(dst_arr, dst_idx), NEWLINE_BEFORE);
    }

    if (flags & AM_LINE) {
        //
        // !!! Testing this heuristic: if someone adds a line to an array
        // with the /LINE flag explicitly, force the head element to have a
        // newline.  This allows `x: copy [] | append/line x [a b c]` to give
        // a more common result.  The head line can be removed easily.
        //
        SET_CELL_FLAG(ARR_HEAD(dst_arr), NEWLINE_BEFORE);
    }

    ASSERT_ARRAY(dst_arr);

    return tail;
}


//
//  Modify_Binary: C
//
// Returns new dst_idx.
//
REBCNT Modify_Binary(
    REBVAL *dst_val,        // target
    REBSTR *verb,            // INSERT, APPEND, CHANGE
    const REBVAL *src_val,  // source
    REBFLGS flags,          // AM_PART
    REBINT dst_len,         // length to remove
    REBINT dups             // dup count
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBSER *dst_ser = VAL_SERIES(dst_val);
    REBCNT dst_idx = VAL_INDEX(dst_val);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (sym != SYM_CHANGE && (flags & AM_PART))
        limit = dst_len; // should be non-negative
    else
        limit = -1;

    if (IS_NULLED(src_val) and sym == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty binary.
        //
        flags |= AM_SPLICE;
        src_val = EMPTY_BINARY;
    }

    if (IS_NULLED(src_val) || limit == 0 || dups < 0)
        return sym == SYM_APPEND ? 0 : dst_idx;

    REBCNT tail = SER_LEN(dst_ser);
    if (sym == SYM_APPEND || dst_idx > tail)
        dst_idx = tail;

    // If the src_val is not a string, then we need to create a string:

    REBCNT src_idx = 0;
    REBCNT src_len;
    REBSER *src_ser;
    bool needs_free;
    if (IS_INTEGER(src_val)) {
        REBI64 i = VAL_INT64(src_val);
        if (i > 255 || i < 0)
            fail ("Inserting out-of-range INTEGER! into BINARY!");

        src_ser = Make_Binary(1);
        *BIN_HEAD(src_ser) = cast(REBYTE, i);
        TERM_BIN_LEN(src_ser, 1);
        needs_free = true;
        limit = -1;
    }
    else if (IS_BLOCK(src_val)) {
        src_ser = Join_Binary(src_val, limit); // NOTE: shared FORM buffer
        needs_free = false;
        limit = -1;
    }
    else if (IS_CHAR(src_val)) {
        //
        // "UTF-8 was originally specified to allow codepoints with up to
        // 31 bits (or 6 bytes). But with RFC3629, this was reduced to 4
        // bytes max. to be more compatible to UTF-16."  So depending on
        // which RFC you consider "the UTF-8", max size is either 4 or 6.
        //
        src_ser = Make_Binary(6);
        SET_SERIES_LEN(
            src_ser,
            Encode_UTF8_Char(BIN_HEAD(src_ser), VAL_CHAR(src_val))
        );
        needs_free = true;
        limit = -1;
    }
    else if (ANY_STRING(src_val)) {
        REBCNT len_at = VAL_LEN_AT(src_val);
        if (limit >= 0 && len_at > cast(REBCNT, limit))
            src_ser = Make_UTF8_From_Any_String(src_val, limit);
        else
            src_ser = Make_UTF8_From_Any_String(src_val, len_at);
        needs_free = true;
        limit = -1;
    }
    else if (IS_BINARY(src_val)) {
        src_ser = NULL;
        needs_free = false;
    }
    else
        fail (src_val);

    // Use either new src or the one that was passed:
    if (src_ser != NULL) {
        src_len = SER_LEN(src_ser);
    }
    else {
        src_ser = VAL_SERIES(src_val);
        src_idx = VAL_INDEX(src_val);
        src_len = VAL_LEN_AT(src_val);
        assert(needs_free == false);
    }

    if (limit >= 0)
        src_len = limit;

    // If Source == Destination we need to prevent possible conflicts.
    // Clone the argument just to be safe.
    // (Note: It may be possible to optimize special cases like append !!)
    if (dst_ser == src_ser) {
        assert(!needs_free);
        src_ser = Copy_Sequence_At_Len(src_ser, src_idx, src_len);
        needs_free = true;
        src_idx = 0;
    }

    // Total to insert:
    //
    REBINT size = dups * src_len;

    if (sym != SYM_CHANGE) {
        // Always expand dst_ser for INSERT and APPEND actions:
        Expand_Series(dst_ser, dst_idx, size);
    } else {
        if (size > dst_len)
            Expand_Series(dst_ser, dst_idx, size - dst_len);
        else if (size < dst_len && (flags & AM_PART))
            Remove_Series_Units(dst_ser, dst_idx, dst_len - size);
        else if (size + dst_idx > tail) {
            EXPAND_SERIES_TAIL(dst_ser, size - (tail - dst_idx));
        }
    }

    // For dup count:
    for (; dups > 0; dups--) {
        memcpy(BIN_AT(dst_ser, dst_idx), BIN_AT(src_ser, src_idx), src_len);
        dst_idx += src_len;
    }

    TERM_SEQUENCE(dst_ser);

    if (needs_free) // didn't use original data as-is
        Free_Unmanaged_Series(src_ser);

    return (sym == SYM_APPEND) ? 0 : dst_idx;
}


//
//  Modify_String: C
//
// Returns new dst_idx.
//
// !!! This routine and Modify_Binary used to be the same function.  However,
// with UTF-8 strings the logic gets more complicated due to the fact that
// "lengths" of ranges (in characters) can be different from "sizes" (in bytes)
// for the amount of content being manipulated.  If that difference can be
// sufficiently abstracted, then the string case's more complex logic could
// apply for the binary case as well...just decaying to where length is equal
// to byte size.  Might be a little bit slower to run binary through the more
// complex handling it doesn't actually require--but less code total.
//
// !!! One issue which might block reunification is the additional concern that
// are created by allowing AS STRING! of a BINARY! and AS BINARY! of a STRING!,
// because modifications through the binary must fail if a mutation of the
// aliased series would produce invalid UTF-8 bytes.  That concern would only
// be applicable to binaries, as all string modifications produce valid UTF-8.
//
REBCNT Modify_String(
    REBVAL *dst,  // ANY-STRING! value to modify (at its current index)
    REBSTR *verb,  // SYM_INSERT, SYM_APPEND, SYM_CHANGE
    const REBVAL *src,  // ANY-VALUE! argument with content to inject
    REBFLGS flags,  // currently just AM_PART
    REBINT dst_len,  // number of codepoints of dst to remove
    REBINT dups  // dup count of how many times to insert the src content
){
    REBSYM sym = STR_SYMBOL(verb);
    assert(sym == SYM_INSERT or sym == SYM_CHANGE or sym == SYM_APPEND);

    REBSER *dst_ser = VAL_SERIES(dst);
    REBCNT dst_idx = VAL_INDEX(dst);

    // For INSERT/PART and APPEND/PART
    //
    REBINT limit;
    if (sym != SYM_CHANGE and (flags & AM_PART)) {
        assert(dst_len >= 0);
        limit = dst_len;
    }
    else
        limit = -1;

    if (IS_NULLED(src) and sym == SYM_CHANGE) {
        //
        // Tweak requests to CHANGE to a null to be a deletion; basically
        // what happens with an empty string.
        //
        flags |= AM_SPLICE;
        src = EMPTY_TEXT;
    }

    if (IS_NULLED(src) or limit == 0 or dups <= 0)
        return sym == SYM_APPEND ? 0 : dst_idx;

    REBCNT tail = SER_LEN(dst_ser);
    if (sym == SYM_APPEND or dst_idx > tail)
        dst_idx = tail;

    // If the src is not an ANY-STRING!, then we need to create string data
    // from the value to use its content.
    //
    // !!! The supporting routines here should write their output into the
    // mold buffer instead of generating entirely new series nodes w/entirely
    // new data allocations.  Data could be used from the buffer, then dropped.
    //
    REBSER *formed = nullptr;  // must free if generated

    const REBYTE *src_ptr;  // start of utf-8 encoded data to insert
    REBCNT src_len;  // length in codepoints
    REBSIZ src_size;  // size in bytes

    if (IS_CHAR(src)) {  // characters store their encoding in their payload
        if (flags & AM_LINE)
            goto form;  // currently need encoding to have the newline in it
        src_ptr = VAL_CHAR_ENCODED(src);
        src_len = 1;
        src_size = VAL_CHAR_ENCODED_SIZE(src);
    }
    else if (IS_BLOCK(src)) {
        //
        // !!! For APPEND and INSERT, the /PART should apply to *block* units,
        // and not character units from the generated string.
        //
        formed = Form_Tight_Block(src);
        src_ptr = STR_HEAD(formed);
        src_len = STR_LEN(formed);
        src_size = SER_USED(formed);
    }
    else if (
        ANY_STRING(src)
        and not IS_TAG(src)  // tags need `<` and `>` to render
    ){
        if (flags & AM_LINE)
            goto form;  // currently need encoding to have the newline in it

        // If Source == Destination we must prevent possible conflicts in
        // the memory regions being moved.  Clone the series just to be safe.
        //
        // !!! It may be possible to optimize special cases like append.
        //
        if (VAL_SERIES(dst) == VAL_SERIES(src))
            goto form;

        src_ptr = VAL_STRING_AT(src);
        src_size = VAL_SIZE_LIMIT_AT(&src_len, src, limit);
    }
    else {
      form:
        formed = Copy_Form_Value(src, 0);
        src_ptr = STR_HEAD(formed);
        src_len = STR_LEN(formed);
        src_size = SER_USED(formed);
    }

    if (limit >= 0)
        src_len = limit;

    // !!! The feature of being able to say APPEND/LINE and get a newline
    // added to the string on each duplicate is new to Ren-C; it's simplest
    // to add it to the formed buffer for now, so the flag forces forming.
    //
    if (flags & AM_LINE) {
        assert(formed);  // don't want to modify input series
        Append_Codepoint(formed, '\n');
        ++src_len;
        ++src_size;
    }

    REBSIZ size = dups * src_size;  // total bytes to insert

    REBSIZ dst_used = SER_USED(dst_ser);
    REBSIZ dst_off = VAL_OFFSET_FOR_INDEX(dst, dst_idx); // !!! review perf

    REBSIZ dst_size = 0xDECAFBAD; // !!! Only calculated for SYM_CHANGE (??)

    if (sym != SYM_CHANGE) {  // Always expand dst_ser for INSERT and APPEND
        Expand_Series(dst_ser, dst_off, size);
    }
    else {  // CHANGE only expands if more content added than overwritten
        dst_size = VAL_SIZE_LIMIT_AT(NULL, dst, dst_len);

        if (size > dst_size)
            Expand_Series(dst_ser, dst_off, size - dst_size);
        else if (size < dst_size and (flags & AM_PART))
            Remove_Series_Units(dst_ser, dst_off, dst_size - size);
        else if (size + dst_off > dst_used) {
            EXPAND_SERIES_TAIL(dst_ser, size - (dst_used - dst_off));
        }
    }

    REBYTE *dst_ptr = SER_SEEK(REBYTE, dst_ser, dst_off);

    REBINT d;
    for (d = 0; d < dups; ++d) {
        memcpy(dst_ptr, src_ptr, src_size);
        dst_ptr += src_size;
        dst_idx += src_len;
    }

    if (sym == SYM_CHANGE)
        TERM_STR_LEN_USED(
            dst_ser,
            tail + (src_len * dups) - dst_len,
            dst_used + size - dst_size
        );
    else
        TERM_STR_LEN_USED(
            dst_ser,
            tail + src_len * dups,
            dst_used + size
        );

    if (formed)  // !!! TBD: Use mold buffer, don't make entire new series
        Free_Unmanaged_Series(formed);  // !!! should just be Drop_Mold()

    REBBMK *bookmark = LINK(dst_ser).bookmarks;
    if (bookmark) {
        assert(not LINK(bookmark).bookmarks);
        RELVAL *mark = ARR_SINGLE(bookmark);
        if (PAYLOAD(Bookmark, mark).index >= dst_idx)
            PAYLOAD(Bookmark, mark).offset += src_size * dups;
    }
    else {
        // We should have generated a bookmark in the process of this
        // modification in most cases where the size is notable.  If we had
        // not, we might add a new bookmark pertinent to the end of the
        // insertion for longer series--since we know the width.
    }

    return (sym == SYM_APPEND) ? 0 : dst_idx;
}
