//
//  File: %sys-stack.h
//  Summary: {Definitions for "Data Stack", "Chunk Stack" and the C stack}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// The data stack and chunk stack are two different data structures for
// temporarily storing REBVALs.  With the data stack, values are pushed one
// at a time...while with the chunk stack, an array of value cells of a given
// length is returned.
//
// A key difference between the two stacks is pointer stability.  Though the
// data stack can accept any number of pushes and then pop the last N pushes
// into a series, each push could potentially change the memory address of
// every other value in the stack.  That's because the data stack is really
// a REBARR series under the hood.  But the chunk stack is a custom structure,
// and guarantees that the address of the values in a chunk will stay stable
// until that chunk is popped.
//
// Another difference is that values on the data stack are implicitly GC safe,
// while clients of the chunk stack needing GC safety must do so manually.
//
// Because of their differences, they are applied to different problems:
//
// A notable usage of the data stack is by REDUCE and COMPOSE.  They use it
// as a buffer for values that are being gathered to be inserted into the
// final array.  It's better to use the data stack as a buffer because it
// means the size of the accumulated result is known before either creating
// a new series or inserting /INTO a target.  This prevents wasting space on
// expansions or resizes and shuffling due to a guessed size.
//
// The chunk stack has an important use as the storage for arguments to
// functions being invoked.  The pointers to these arguments are passed by
// natives through the stack to other routines, which may take arbitrarily
// long to return...and may call code involving many data stack pushes and
// pops.  Argument pointers must be stable, so using the data stack would
// not work.  Also, to efficiently implement argument fulfillment without
// pre-filling the cells, uninitialized memory is allowed in the chunk stack
// across potentical garbage collections.  This means implicit GC protection
// can't be performed, with a subset of valid cells marked by the frame. 
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATA STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The data stack (DS_) is for pushing one individual REBVAL at a time.  The
// values can then be popped in a Last-In-First-Out way.  It is also possible
// to mark a stack position, do any number of pushes, and then ask for the
// range of values pushed since the mark to be placed into a REBARR array.
// As long as a value is on the data stack, any series it refers to will be
// protected from being garbage-collected.
//
// The data stack has many applications, and can be used by any piece of the
// system.  But there is a rule that when that piece is finished, it must
// "balance" the stack back to where it was when it was called!  There is
// a check in the main evaluator loop that the stack has been balanced to
// wherever it started by the time a function call ends.  However, it is not
// necessary to balance the stack in the case of calling a `fail`--because
// it will be automatically restored to where it was at the PUSH_TRAP().
//
// To speed pushes and pops to the stack while also making sure that each
// push is tested to see if an expansion is needed, a trick is used.  This
// trick is to grow the stack in blocks, and always maintain that the block
// has an END marker at its point of capacity--and ensure that there are no
// end markers between the DSP and that capacity.  This way, if a push runs
// up against an END it knows to do an expansion.
//

// DSP stands for "(D)ata (S)tack "(P)osition", and is the index of the top
// of the data stack (last valid item in the underlying array)
//
#define DSP \
    DS_Index

// DS_AT accesses value at given stack location.  It is allowed to point at
// a stack location that is an end, e.g. DS_AT(dsp + 1), because that location
// may be used as the start of a copy which is ultimately of length 0.
//
inline static REBVAL *DS_AT(REBDSP d) {
    REBVAL *v = DS_Movable_Base + d;
    assert(
        ((v->header.bits & NODE_FLAG_CELL) and d <= (DSP + 1))
        or ((v->header.bits & NODE_FLAG_END) and d == (DSP + 1))
    );
    return v;
}

// DS_TOP is the most recently pushed item
//
#define DS_TOP \
    DS_AT(DSP)

#if !defined(NDEBUG)
    #define IN_DATA_STACK_DEBUG(v) \
        IS_VALUE_IN_ARRAY_DEBUG(DS_Array, (v))
#endif

//
// PUSHING
//
// If you push "unsafe" trash to the stack, it has the benefit of costing
// nothing extra in a release build for setting the value (as it is just
// left uninitialized).  But you must make sure that a GC can't run before
// you have put a valid value into the slot you pushed.
//
// If the stack runs out of capacity then it will be expanded by the basis
// defined below.  The number is arbitrary and should be tuned.  Note the
// number of bytes will be sizeof(REBVAL) * STACK_EXPAND_BASIS
//

#define STACK_EXPAND_BASIS 128

// Note: DS_Movable_Base + DSP is just DS_TOP, but it asserts on ENDs.
//
#define DS_PUSH_TRASH \
    (++DSP, IS_END(DS_Movable_Base + DSP) \
        ? Expand_Data_Stack_May_Fail(STACK_EXPAND_BASIS) \
        : TRASH_CELL_IF_DEBUG(DS_Movable_Base + DSP))

inline static void DS_PUSH(const REBVAL *v) {
    DS_PUSH_TRASH;
    Move_Value(DS_TOP, v);
}


//
// POPPING
//
// Since it's known that END markers were never pushed, a pop can just leave
// whatever bits had been previously pushed, dropping only the index.  The
// only END marker will be the one indicating the tail of the stack.  
//

#ifdef NDEBUG
    #define DS_DROP \
        (--DS_Index)

    #define DS_DROP_TO(dsp) \
        (DS_Index = dsp)
#else
    inline static void DS_DROP_Core(void) {
        // Note: DS_TOP checks to make sure it's not an END.
        Init_Unreadable_Blank(DS_TOP); // TRASH would mean ASSERT_ARRAY failing
        --DS_Index;
    }

    #define DS_DROP \
        DS_DROP_Core()

    inline static void DS_DROP_TO_Core(REBDSP dsp) {
        assert(DSP >= dsp);
        while (DSP != dsp)
            DS_DROP;
    }

    #define DS_DROP_TO(dsp) \
        DS_DROP_TO_Core(dsp)
#endif

// If Pop_Stack_Values_Core is used ARRAY_FLAG_FILE_LINE, it means the system
// will try to capture the file and line number associated with the current
// frame into the generated array.  But if there are other flags--like
// ARRAY_FLAG_PARAMLIST or ARRAY_FLAG_VARLIST--it's assumed that you don't
// want to do this, because the ->link and ->misc fields have other uses.
//
#define Pop_Stack_Values(dsp) \
    Pop_Stack_Values_Core((dsp), ARRAY_FLAG_FILE_LINE)

#define Pop_Stack_Values_Keep_Eval_Flip(dsp) \
    Pop_Stack_Values_Core((dsp), ARRAY_FLAG_FILE_LINE | ARRAY_FLAG_VOIDS_LEGAL)


//=////////////////////////////////////////////////////////////////////////=//
//
//  CHUNK STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unlike the data stack, values living in the chunk stack are not implicitly
// protected from garbage collection.
//
// Also, unlike the data stack, the chunk stack allows the pushing and popping
// of arbitrary-sized arrays of values which will not be relocated during
// their lifetime.
//
// This is accomplished using a custom "chunked" allocator.  The two structs
// involved are a list of "Chunkers", which internally have a list of
// "Chunks" threaded between them.  The method keeps one spare chunker
// allocated, and only frees a chunker when a full chunker prior has the last
// element popped out of it.  In memory it looks like this:
//
//      [chunker->next
//          ([info (->offset,size)][value1][value2][value3]...)   // chunk 1
//          ([info (->offset,size)][value1]...)                   // chunk 2
//          ([info (->offset,size)][value1][value2]...)           // chunk 3
//          ...remaining payload space in chunker...
//      ]
//
// Since the chunker size is a known constant, it's possible to quickly deduce
// the chunker a chunk lives in from its pointer and the remaining payload
// amount in the chunker.
//

struct Reb_Chunker {
    RELVAL info;
    RELVAL values[1];
};

#define CS_CHUNKER_MIN_LEN (4096 / sizeof(REBVAL))


struct Reb_Chunk {
    //
    // Chunk starts with a REB_0_CHUNK value, with the NODE_FLAG_END bit set,
    // so it can implicitly terminate the preceding chunk.  ->extra contains
    // the pointer to the previous chunk.  ->payload has the length of the
    // chunk and the address of the containing chunker.
    //
    RELVAL subinfo;

    // The `values` is an array whose real size exceeds the struct.  (It is
    // set to a size of one because it cannot be [0] if built with C++.)
    // When the value pointer is given back to the user, the address of
    // this array is how they speak about the chunk itself.
    //
    // Next chunk's `subinfo` header serves as an END marker for this array.
    //
    RELVAL subvalues[1]; // can't use REBVAL (has constructor in C++ build)
};

#define CHUNKER_NEXT(c) \
    (c)->info.extra.next_chunker

#define CHUNKER_AVAIL(c) \
    (c)->info.payload.chunker.avail

#define CHUNK_LEN(c) \
    (c)->subinfo.payload.chunk.len

#define CHUNK_PREV(c) \
    (c)->subinfo.extra.prev_chunk

#define CHUNK_CHUNKER(c) \
    (c)->subinfo.payload.chunk.chunker

#define CHUNK_FROM_VALUES(v) \
    cast(struct Reb_Chunk *, v - 1)


// This doesn't necessarily call Alloc_Mem, because chunks are allocated
// sequentially inside of "chunker" blocks, in their ordering on the stack.
// Allocation is only required if we need to step into a new chunk (and even
// then only if we aren't stepping into a chunk that we are reusing from
// a prior expansion).
//
// The "Ended" indicates that there is no need to manually put an end in the
// `num_values` slot.  Chunks are implicitly terminated by their layout,
// because the REB_0_CHUNK of the subsequent chunk in a chunker terminates.
//
inline static REBVAL* Push_Value_Chunk_Of_Length(REBCNT num_values) {
    struct Reb_Chunker *chunker = CHUNK_CHUNKER(TG_Top_Chunk); // never NULL

    struct Reb_Chunk *chunk;
    if (CHUNKER_AVAIL(chunker) >= num_values + 2) { // needs subinfo and END
        chunk = cast(struct Reb_Chunk*,
            TG_Top_Chunk->subvalues + CHUNK_LEN(TG_Top_Chunk)
        );
        CHUNK_CHUNKER(chunk) = chunker;
        CHUNKER_AVAIL(chunker) -= num_values + 1; // don't count END
    }
    else { // Topmost chunker has insufficient space
        REBCNT alloc_len = CS_CHUNKER_MIN_LEN; // default allocation amount

        struct Reb_Chunker *next = CHUNKER_NEXT(chunker);
        if (next) {
            //
            // An empty previous chunker is still allocated--but only one
            // unused one should be kept around.
            //
            assert(not CHUNKER_NEXT(next));

            REBCNT avail = CHUNKER_AVAIL(next);
            if (avail >= num_values + 2) { // needs subinfo and END
                CHUNKER_AVAIL(next) -= (num_values + 1); // don't count END
                chunk = cast(struct Reb_Chunk*, &next->values);
            }
            else {
                Free_Mem(next, (avail + 1) * sizeof(REBVAL)); // include info
                alloc_len = num_values // for the values
                    + 1 // for this chunk's subinfo
                    + 1 // for the *next* subinfo, doubles as END marker
                    + 1; // for the chunker's info

                assert(alloc_len > CS_CHUNKER_MIN_LEN); // else why realloc?
                goto alloc_chunk;
            }
        }
        else {
        alloc_chunk:
            next = cast(struct Reb_Chunker*,
                Alloc_Mem(alloc_len * sizeof(REBVAL))
            );
            Prep_Stack_Cell(&next->info);
            SET_END(&next->info); // "REB_0_CHUNKER"
            CHUNKER_NEXT(next) = nullptr;
            CHUNKER_AVAIL(next) = alloc_len
                - 1 // for the chunker's info
                - 1 // for the chunk's subinfo
                - num_values; // for the values (don't count END)

            CHUNKER_NEXT(chunker) = next;

            // Format the cells as stack, which should be reusable between
            // calls that push and pop chunkers in the same space.
            //
            // !!! While this sets the bits conveying CELL_FLAG_STACK, there
            // are concepts to encode the stack level (integer) into cells
            // in 64-bit builds.  This would require updating those bits on
            // each push, though Do_Core() could do it as it went...though it
            // would need to check to be sure it was only doing so if the
            // values it was writing into for the frame were stack-based.
            //
            REBCNT n;
            for (n = 0; n < alloc_len - 1; ++n)
                Prep_Stack_Cell(&next->values[n]);

            chunk = cast(struct Reb_Chunk*, &next->values);
            SET_END(&chunk->subinfo);
            chunk->subinfo.header.bits |= CELL_FLAG_PROTECTED;
        }

        CHUNK_CHUNKER(chunk) = next;
    }

  #if !defined(NDEBUG)
    assert(IS_END(&chunk->subinfo)); // "REB_0_CHUNK"
    assert(chunk->subinfo.header.bits & CELL_FLAG_PROTECTED);
  #endif
    CHUNK_LEN(chunk) = num_values;
    CHUNK_PREV(chunk) = TG_Top_Chunk;

    // Set header in next element to 0, so it can serve as a terminator
    // for the data range of this until it gets instantiated (if ever)
    //
    SET_END(&chunk->subvalues[num_values]);

    TG_Top_Chunk = chunk;

    // Values are Prep_Stack_Cell() as trash when the chunker was allocated,
    // restored to trash when chunks are dropped.  Trash cells aren't GC-safe,
    // so Mark_Frame_Stack_Deep() must be careful to only mark cells that
    // have been  filled.  This is handled by the REBFRM* keeping track of
    // the ->arg position, and making that available to the GC.
    //
  #if !defined(NDEBUG)
    chunk->subinfo.header.bits |= CELL_FLAG_PROTECTED;
    #ifdef DEBUG_CHUNK_STACK
        REBCNT index;
        for (index = 0; index < num_values; index++)
            assert(IS_TRASH_DEBUG(&chunk->subvalues[index]));
    #endif
    chunk->subvalues[num_values].header.bits |= CELL_FLAG_PROTECTED;
  #endif

    assert(CHUNK_FROM_VALUES(&chunk->subvalues[0]) == chunk);
    return cast(REBVAL*, &chunk->subvalues[0]);
}


// Free an array of previously pushed REBVALs.  This only occasionally
// requires an actual call to Free_Mem(), as the chunks are allocated
// sequentially inside containing allocations.
//
inline static void Drop_Chunk_Of_Values(REBVAL *opt_head)
{
    struct Reb_Chunk* chunk = TG_Top_Chunk;

    // Passing in `opt_head` is optional, but a good check to make sure you are
    // actually dropping the chunk you think you are.  (On an error condition
    // when dropping chunks to try and restore the top chunk to a previous
    // state, this information isn't available.)
    //
  #if defined(NDEBUG)
    UNUSED(opt_head);
  #else
    assert(not opt_head or chunk == CHUNK_FROM_VALUES(opt_head));
  #endif

    TG_Top_Chunk = CHUNK_PREV(chunk); // drop to prior chunk

    struct Reb_Chunker *chunker = CHUNK_CHUNKER(chunk);

    if (cast(RELVAL*, chunk) == &chunker->values[0]) {
        //
        // This chunk sits at the head of a chunker.
        //
        // When we've completely emptied a chunker, we check to see if the
        // chunker after it is still live.  If so, we free it.  But we
        // want to keep *this* just-emptied chunker alive for overflows if we
        // rapidly get another push, to avoid Make_Mem()/Free_Mem() costs.

        assert(TG_Top_Chunk);

        struct Reb_Chunker *next = CHUNKER_NEXT(chunker);
        if (next) {
            Free_Mem(next, (CHUNKER_AVAIL(next) + 1) * sizeof(REBVAL));
            CHUNKER_NEXT(chunker) = nullptr;
        }
    }

    CHUNKER_AVAIL(chunker) += CHUNK_LEN(chunk) + 1;

    // In debug builds we poison the memory for the chunk... but not the `prev`
    // pointer because we expect that to stick around!
    //
  #if !defined(NDEBUG)
    assert(
        chunk->subvalues[CHUNK_LEN(chunk)].header.bits & CELL_FLAG_PROTECTED
    ); // the next END should have protection...
    chunk->subvalues[CHUNK_LEN(chunk)].header.bits &= ~CELL_FLAG_PROTECTED;
    REBCNT n;
    for (n = 0; n < CHUNK_LEN(chunk) + 1; ++n) // + 1 to clear out next END
        TRASH_CELL_IF_DEBUG(&chunk->subvalues[n]);
    assert(IS_END(cast(REBVAL*, chunk))); // but leave *this* END
  #endif
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  C STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol doesn't want to crash in the event of a stack overflow, but would
// like to gracefully trap it and return the user to the console.  While it
// is possible for Rebol to set a limit to how deeply it allows function
// calls in the interpreter to recurse, there's no *portable* way to
// catch a stack overflow in the C code of the interpreter itself.
//
// Hence, by default Rebol will use a non-standard heuristic.  A flag is
// passed to say if OS_STACK_GROWS_UP.  If so, it then extrapolates that C
// function call frames will be laid out consecutively, and the memory
// difference between a stack variable in the topmost stacks can be checked
// against some limit.
//
// This has nothing to do with guarantees in the C standard, and compilers
// can really put variables at any address they feel like:
//
// http://stackoverflow.com/a/1677482/211160
//
// Additionally, it puts the burden on every recursive or deeply nested
// routine to sprinkle calls to the C_STACK_OVERFLOWING macro somewhere
// in it.  The ideal answer is to make Rebol itself corral an interpreted
// script such that it can't cause the C code to stack overflow.  Lacking
// that ideal this technique could break, so build configurations should
// be able to turn it off if needed.
//
// In the meantime, C_STACK_OVERFLOWING is a macro which takes the
// address of some variable local to the currently executed function.
// Note that because the limit is noticed before the C stack has *actually*
// overflowed, you still have a bit of stack room to do the cleanup and
// raise an error trap.  (You need to take care of any unmanaged series
// allocations, etc).  So cleaning up that state should be doable without
// making deep function calls.
//
// !!! Future approaches should look into use of Windows stack exceptions
// or libsigsegv:
//
// http://stackoverflow.com/questions/5013806/
//


#if defined(OS_STACK_GROWS_UP)

    #define C_STACK_OVERFLOWING(address_of_local_var) \
        (cast(uintptr_t, (address_of_local_var)) >= TG_Stack_Limit)

#elif defined(OS_STACK_GROWS_DOWN)

    #define C_STACK_OVERFLOWING(address_of_local_var) \
        (cast(uintptr_t, (address_of_local_var)) <= TG_Stack_Limit)

#else

    #define C_STACK_OVERFLOWING(address_of_local_var) \
        (TG_Stack_Grows_Up \
            ? cast(uintptr_t, (address_of_local_var)) >= TG_Stack_Limit \
            : cast(uintptr_t, (address_of_local_var)) <= TG_Stack_Limit)
#endif

#define STACK_BOUNDS (2*1024*1024) // note: need a better way to set it !!
// Also: made somewhat smaller than linker setting to allow trapping it

// Since stack overflows are memory-related errors, don't try to do any
// error allocations...just use an already made error.
//
#define Fail_Stack_Overflow() \
    fail (VAL_CONTEXT(Root_Stackoverflow_Error));
