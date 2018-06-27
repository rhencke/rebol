//
//  File: %sys-rebnod.h
//  Summary: {Definitions for the Rebol_Header-having "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// In order to implement several "tricks", the first pointer-size slots of
// many datatypes is a `Reb_Header` structure.  The bit layout of this header
// is chosen in such a way that not only can Rebol value pointers (REBVAL*)
// be distinguished from Rebol series pointers (REBSER*), but these can be
// discerned from a valid UTF-8 string just by looking at the first byte.
//
// On a semi-superficial level, this permits a kind of dynamic polymorphism,
// such as that used by panic():
//
//     REBVAL *value = ...;
//     panic (value); // can tell this is a value
//
//     REBSER *series = ...;
//     panic (series) // can tell this is a series
//
//     const char *utf8 = ...;
//     panic (utf8); // can tell this is UTF-8 data (not a series or value)
//
// But a more compelling case is the usage through the API, so variadic
// combinations of strings and values can be intermixed, as in:
//
//     rebRun("poke", series, "1", value, END) 
//
// Internally, the ability to discern these types helps certain structures or
// arrangements from having to find a place to store a kind of "flavor" bit
// for a stored pointer's type.  They can just check the first byte instead.
//
// For lack of a better name, the generic type covering the superclass is
// called a "Rebol Node".
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE HEADER a.k.a `struct Reb_Header` (for REBVAL and REBSER uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// platform-sized integer...while still being able to control the underlying
// ordering of those bits in memory.  See FLAG_LEFT_BIT() in %reb-c.h for how
// this is achieved.
//
// This control allows the leftmost byte of a Rebol header (the one you'd
// get by casting REBVAL* to an unsigned char*) to always start with the bit
// pattern `10`.  This pattern corresponds to what UTF-8 calls "continuation
// bytes", which may never legally start a UTF-8 string:
//
// https://en.wikipedia.org/wiki/UTF-8#Codepage_layout
//
// There are also applications of Reb_Header as an "implicit terminator".
// Such header patterns don't actually start valid REBNODs, but have a bit
// pattern able to signal the IS_END() test for REBVAL.  See notes on
// CELL_FLAG_END and NODE_FLAG_CELL.
//

struct Reb_Header {
    //
    // unsigned integer that's the size of a platform pointer (e.g. 32-bits on
    // 32 bit platforms and 64-bits on 64 bit machines).  See macros like
    // FLAG_LEFT_BIT() for how these bits are laid out in a special way.
    //
    // !!! Future application of the 32 unused header bits on 64-bit machines
    // might add some kind of optimization or instrumentation, though the
    // unused bits are currently in weird byte positions.
    //
    uintptr_t bits;
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_NODE (leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// For the sake of simplicity, the leftmost bit in a node is always one.  This
// is because every UTF-8 string starting with a bit pattern 10xxxxxxx in the
// first byte is invalid.
//
// Warning: Previous attempts to multiplex this with an information-bearing
// bit were tricky, and wound up ultimately paying for a fixed bit in some
// other situations.  Better to sacrifice the bit and keep it straightforward.
//
#define NODE_FLAG_NODE \
    FLAG_LEFT_BIT(0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_FREE (second-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The second-leftmost bit will be 0 for all Reb_Header in the system that
// are "valid".  This completes the plan of making sure all REBVAL and REBSER
// that are usable will start with the bit pattern 10xxxxxx, hence not be
// confused with a string...since that always indicates an invalid leading
// byte in UTF-8.
//
// The exception are freed nodes, but they use 11000000 and 110000001 for
// freed REBSER nodes and "freed" value nodes (trash).  These are the bytes
// 192 and 193, which are specifically illegal in any UTF8 sequence.  So
// even these cases may be safely distinguished from strings.  See the
// NODE_FLAG_CELL for why it is chosen to be that 8th bit.
//
#define NODE_FLAG_FREE \
    FLAG_LEFT_BIT(1)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_MANAGED (third-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The GC-managed bit is used on series to indicate that its lifetime is
// controlled by the garbage collector.  If this bit is not set, then it is
// still manually managed...and during the GC's sweeping phase the simple fact
// that it isn't NODE_FLAG_MARKED won't be enough to consider it for freeing.
//
// See MANAGE_SERIES for details on the lifecycle of a series (how it starts
// out manually managed, and then must either become managed or be freed
// before the evaluation that created it ends).
//
// Note that all scanned code is expected to be managed by the GC (because
// walking the tree after constructing it to add the "manage GC" bit would be
// expensive, and we don't load source and free it manually anyway...how
// would you know after running it that pointers inside weren't stored?)
//
#define NODE_FLAG_MANAGED \
    FLAG_LEFT_BIT(2)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_MARKED (fourth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This flag is used by the mark-and-sweep of the garbage collector, and
// should not be referenced outside of %m-gc.c.
//
// See `SERIES_INFO_BLACK` for a generic bit available to other routines
// that wish to have an arbitrary marker on series (for things like
// recursion avoidance in algorithms).
//
// Because "pairings" can wind up marking what looks like both a value cell
// and a series, it's a bit dangerous to try exploiting this bit on a generic
// REBVAL.  If one is *certain* that a value is not "paired" (for instance,
// not an API REBVAL) then values can use it for other things.
//
#define NODE_FLAG_MARKED \
    FLAG_LEFT_BIT(3)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_4 (fifth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unused, as of yet.
//
#define NODE_FLAG_4 \
    FLAG_LEFT_BIT(4)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_ROOT (sixth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Means the node should be treated as a root for GC purposes.  If the node
// also has NODE_FLAG_CELL, that means the cell must live in a "pairing"
// REBSER-sized structure for two cells.  This indicates it is an API handle.
//
// This flag is masked out by CELL_MASK_COPIED, so that when values are moved
// into or out of API handle cells the flag is left untouched.
//
#define NODE_FLAG_ROOT \
    FLAG_LEFT_BIT(5)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_STACK (seventh-leftmost bit)
//  aliased as CELL_FLAG_STACK and SERIES_FLAG_STACK
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When writing to a value cell, it is sometimes necessary to know how long
// that cell will "be alive".  This is important if there is some stack-based
// transient structure in the source cell, which would need to be converted
// into something longer-lived if the destination cell will outlive it.
//
// Hence cells must be formatted to say whether they are CELL_FLAG_STACK or
// not, before any writing can be done to them.  If they are not then they
// are presumed to be indefinite lifetime (e.g. cells resident inside of an
// array managed by the garbage collector).
//
// But if a cell is marked with CELL_FLAG_STACK, that means it is expected
// that scanning *backwards* in memory will find a specially marked REB_FRAME
// cell, which will lead to the frame to whose lifetime the cell is bound.
//
// !!! This feature is a work in progress.
//
// For series, varlists of FRAME! are also marked with this to indicates that
// a context's varlist data lives on the stack.  That means that when the
// action terminates, the data will no longer be accessible (so
// SERIES_INFO_INACCESSIBLE will be true).
//
#define NODE_FLAG_STACK \
    FLAG_LEFT_BIT(6)

#define CELL_FLAG_STACK NODE_FLAG_STACK
#define SERIES_FLAG_STACK NODE_FLAG_STACK


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_CELL (eighth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// If this bit is set in the header, it indicates the slot the header is for
// is `sizeof(REBVAL)`.
//
// In the debug build, it provides safety for all value writing routines,
// including avoiding writing over "implicit END markers" (which have
// CELL_FLAG_END set, but are backed only by `sizeof(struct Reb_Header)`.
//
// In the release build, it distinguishes "pairing" nodes (holders for two
// REBVALs in the same pool as ordinary REBSERs) from an ordinary REBSER node.
// Plain REBSERs have the cell mask clear, while pairing values have it set.
//
// The position chosen is not random.  It is picked as the 8th bit from the
// left so that freed nodes can still express a distinction between
// being a cell and not, due to 11000000 (192) and 11000001 (193) are both
// invalid UTF-8 bytes, hence these two free states are distinguishable from
// a leading byte of a string.
//
#define NODE_FLAG_CELL \
    FLAG_LEFT_BIT(7)


// v-- BEGIN GENERAL CELL AND SERIES BITS WITH THIS INDEX

#define GENERAL_CELL_BIT 8
#define GENERAL_SERIES_BIT 8


// There are two special invalid bytes in UTF8 which have a leading "110"
// bit pattern, which are freed nodes.  These two patterns are for freed bytes
// and "freed cells"...though NODE_FLAG_FREE is not generally used on purpose
// (mostly happens if reading uninitialized memory)
//
#define FREED_SERIES_BYTE 192
#define FREED_CELL_BYTE 193


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE STRUCTURE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Though the name Node is used for a superclass that can be "in use" or
// "free", this is the definition of the structure for its layout when it
// has NODE_FLAG_FREE set.  In that case, the memory manager will set the
// header bits to have the leftmost byte as FREED_SERIES_BYTE, and use the
// pointer slot right after the header for its linked list of free nodes.
//

struct Reb_Node {
    struct Reb_Header header; // leftmost byte FREED_SERIES_BYTE if free

    struct Reb_Node *next_if_free; // if not free, entire node is available

    // Size of a node must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
};

#ifdef NDEBUG
    #define IS_FREE_NODE(p) \
        (did (cast(struct Reb_Node*, (p))->header.bits & NODE_FLAG_FREE))
#else
    inline static REBOOL IS_FREE_NODE(void *p) {
        struct Reb_Node *n = cast(struct Reb_Node*, p);

        if (not (n->header.bits & NODE_FLAG_FREE))
            return false;

        assert(
            FIRST_BYTE(n->header) == FREED_SERIES_BYTE
            or FIRST_BYTE(n->header) == FREED_CELL_BYTE
        );
        return true;
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// MEMORY ALLOCATION AND FREEING MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's internal memory management is done based on a pooled model, which
// use Alloc_Mem and Free_Mem instead of calling malloc directly.  (See the
// comments on those routines for explanations of why this was done--even in
// an age of modern thread-safe allocators--due to Rebol's ability to exploit
// extra data in its pool block when a series grows.)
//
// Since Free_Mem requires the caller to pass in the size of the memory being
// freed, it can be tricky.  These macros are modeled after C++'s new/delete
// and new[]/delete[], and allocations take either a type or a type and a
// length.  The size calculation is done automatically, and the result is cast
// to the appropriate type.  The deallocations also take a type and do the
// calculations.
//
// In a C++11 build, an extra check is done to ensure the type you pass in a
// FREE or FREE_N lines up with the type of pointer being freed.
//

// !!! Definitions for the memory allocator generally don't need to be
// included by all clients, though currently it is necessary to indicate
// whether a "node" is to be allocated from the REBSER pool or the REBGOB
// pool.  Hence, the REBPOL has to be exposed to be included in the
// function prototypes.  Review this necessity when REBGOB is changed.
//
typedef struct rebol_mem_pool REBPOL;

#define ALLOC(t) \
    cast(t *, Alloc_Mem(sizeof(t)))

#define ALLOC_ZEROFILL(t) \
    cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define ALLOC_N(t,n) \
    cast(t *, Alloc_Mem(sizeof(t) * (n)))

#define ALLOC_N_ZEROFILL(t,n) \
    cast(t *, memset(ALLOC_N(t, (n)), '\0', sizeof(t) * (n)))

#ifdef CPLUSPLUS_11
    #define FREE(t,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE type" \
            ); \
            Free_Mem(p, sizeof(t)); \
        } while (0)

    #define FREE_N(t,n,p)   \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE_N type" \
            ); \
            Free_Mem(p, sizeof(t) * (n)); \
        } while (0)
#else
    #define FREE(t,p) \
        Free_Mem((p), sizeof(t))

    #define FREE_N(t,n,p)   \
        Free_Mem((p), sizeof(t) * (n))
#endif

#define CLEAR(m, s) \
    memset((void*)(m), 0, s)

#define CLEARS(m) \
    memset((void*)(m), 0, sizeof(*m))


// The GHOST pointer is something that can be used in places that might use a
// nullptr otherwise, but it has the advantage of being able to avoid checking
// for null before dereferencing it.  So instead of writing:
//
//    if (x != nullptr and GET_SER_FLAG(x, ...))
//
// You can use GHOST and just say `GET_SER_FLAG(x, ...)` on it, and it will
// fail for nearly everything...except NODE_FLAG_CELL and NODE_FLAG_MANAGED.
// These are chosen for tactical reasons of their use in UNBOUND, but note
// that ghost does not have NODE_FLAG_NODE set.
//
#if defined(__cplusplus) and defined(REB_DEF)
    class GHOST_Cpp { // C++ won't allow assign/compare void* to any pointer
      private:
        GHOST_Cpp() {}
      public:
        static GHOST_Cpp ghost() { return GHOST_Cpp (); }
        operator void*();
        operator REBNOD*();
        operator REBFRM*();
        operator REBSER*();
        operator REBARR*();
        operator REBACT*();
        operator REBCTX*();
    };
    #define GHOST \
        GHOST_Cpp::ghost()
#else
    #define GHOST \
        cast(void*, &PG_Ghost) // C allows assign/compare void* to any pointer
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// "GHOSTABLE" : REDUCE LIKELIHOOD THAT A POINTER IS NULL
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This is a C-biased adaptation of `non_null` from the C++ Core Guidelines:
//
// https://github.com/isocpp/CppCoreGuidelines
//
// It is abridged code from Microsoft's MIT-Licensed implementation, removing
// dependencies on complex pre/post conditions and moving to a simple assert:
//
// https://github.com/Microsoft/GSL/blob/master/include/gsl/gsl_assert
//
//     "Has zero size overhead over T.
//
//     If T is a pointer (i.e. T == U*) then
//     - allow construction from U*
//     - disallow construction from nullptr_t
//     - disallow default construction
//     - ensure construction from null U* fails
//     - allow implicit conversion to U*"
//
// Notably, this does not support pointer incrementation and other operators,
// assuming it points to a single object.  Casting to the raw pointer type
// can work around that, while losing checking.
//
// This weaker version is usable from code that otherwise compiles as C, as
// it doesn't make structures non-default constructible to enforce the rule.
// Hence an uninitialized pointer might wind up holding null.  It uses
// assert instead of more complex mechanics, removes `constexpr` for GCC 4.8
// compatibility (used on Travis), it also doesn't use std::enable_if_t.
//
// It is specifically to assist checking when GHOST is used instead of nullptr
// to indicate a disengaged state, as it's easy to forget and compare with
// null or assign null.
//
// It requires DEBUG_CHECK_CASTS because otherwise, SER() and NOD() are just
// macros, and thus are not powerful enough to do tricky type conversions.
//
#if !defined(CPLUSPLUS_11) || !defined(REB_DEF) || !defined(DEBUG_CHECK_CASTS)
    #define GHOSTABLE(ptr_type) \
        ptr_type
#else
    #include <utility> // for std::forward

    template <class T>
    class ghostable {
      private:
        T ptr_;

      public:
        // A key difference between not_null and ghostable is that it
        // allows default construction.
        //
        ghostable() = default; // possibly uninitialized, including null/0

        // It also allows assignments from other pointers (including GHOST).
        //
        // !!! Checking on assignment and on get() is probably overkill; best
        // to probably pick one or the other.
        //
        ghostable(T&& other)
          { assert(other != nullptr); ptr_ = other; }
        ghostable(const T other)
          { assert(other != nullptr); ptr_ = other; }
        ghostable& operator=(const T& other)
          { assert(other != nullptr); ptr_ = other; return *this; }

        static_assert(
            std::is_assignable<T&, std::nullptr_t>::value,
            "T cannot be assigned nullptr."
        );

        template <
            typename U,
            typename = typename std::enable_if<
                std::is_convertible<U, T>::value
            >::type
        >
        explicit ghostable(U&& u) : ptr_(std::forward<U>(u))
          { assert(ptr_ != nullptr); }

        template <
            typename = typename std::enable_if<
                !std::is_same<ghostable, T>::value
            >::type
        >
        explicit ghostable(T u) : ptr_(u)
          { assert(ptr_ != nullptr); }

        template <
            typename U,
            typename = typename std::enable_if<
                std::is_convertible<U, T>::value
            >::type
        >
        ghostable(const ghostable<U>& other)
            : ghostable(other.get())
          {}

        ghostable(ghostable&& other) = default;
        ghostable(const ghostable& other) = default;
        ghostable& operator=(const ghostable& other) = default;

        T get() const
          { assert(ptr_ != nullptr); return ptr_; }

        explicit operator REBACT*() const
          { return reinterpret_cast<REBACT*>(get()); }
        explicit operator REBCTX*() const
          { return reinterpret_cast<REBCTX*>(get()); }
        explicit operator REBFRM*() const
          { return reinterpret_cast<REBFRM*>(get()); }
        explicit operator REBSER*() const
          { return reinterpret_cast<REBSER*>(get()); }
        explicit operator REBARR*() const
          { return reinterpret_cast<REBARR*>(get()); }

        operator T() const { return get(); }
        T operator->() const { return get(); }
        typename std::remove_pointer<T>::type operator*() const
          { return *get(); } 

        ghostable(std::nullptr_t) = delete;
        ghostable& operator=(std::nullptr_t) = delete;

        ghostable& operator++() = delete;
        ghostable& operator--() = delete;
        ghostable operator++(int) = delete;
        ghostable operator--(int) = delete;
        ghostable& operator+=(std::ptrdiff_t) = delete;
        ghostable& operator-=(std::ptrdiff_t) = delete;
        void operator[](std::ptrdiff_t) const = delete;

        // https://stackoverflow.com/q/51057099/211160
        //
        bool operator==(std::nullptr_t rhs) = delete;
        bool operator!=(std::nullptr_t rhs) = delete;
    };

    // https://stackoverflow.com/q/51057099/211160
    //
    template <class U>
    bool operator==(std::nullptr_t lhs, const ghostable<U> &rhs) = delete;
    template <class U>
    bool operator!=(std::nullptr_t lhs, const ghostable<U> &rhs) = delete;

    template <class T, class U>
    auto operator==(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() == rhs.get())
      { return lhs.get() == rhs.get(); }

    template <class T, class U>
    auto operator!=(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() != rhs.get())
      {  return lhs.get() != rhs.get(); }

    template <class T, class U>
    auto operator<(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() < rhs.get())
      { return lhs.get() < rhs.get(); }

    template <class T, class U>
    auto operator<=(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() <= rhs.get())
      { return lhs.get() <= rhs.get(); }

    template <class T, class U>
    auto operator>(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() > rhs.get())
      { return lhs.get() > rhs.get(); }

    template <class T, class U>
    auto operator>=(const ghostable<T>& lhs, const ghostable<U>& rhs)
        -> decltype(lhs.get() >= rhs.get())
      {  return lhs.get() >= rhs.get(); }

    template <class T, class U>
    std::ptrdiff_t operator-(const ghostable<T>&, const ghostable<U>&) = delete;
    template <class T>
    ghostable<T> operator-(const ghostable<T>&, std::ptrdiff_t) = delete;
    template <class T>
    ghostable<T> operator+(const ghostable<T>&, std::ptrdiff_t) = delete;
    template <class T>
    ghostable<T> operator+(std::ptrdiff_t, const ghostable<T>&) = delete;

    #define GHOSTABLE(ptr_type) \
        ghostable<ptr_type>
#endif
