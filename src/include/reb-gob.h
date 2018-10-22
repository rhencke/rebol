//
//  File: %reb-gob.h
//  Summary: "Graphical compositing objects"
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
// GOBs are lower-level graphics object used by the compositing and rendering
// system of the /View system of R3-Alpha.  They represented either individual
// pieces of a GUI control (panes and their children) or top-level windows
// themselves.
//
// Because a GUI could contain thousands of GOBs, it was believed that they
// could not be implemented as ordinary OBJECT!s.  Instead they were made as
// small fixed-size objects (somewhat parallel to REBSER) which held pointers
// to dynamic series data, like pane lists or associated user data.  Because
// they held pointers to Rebol nodes, they had to have custom behavior in
// the garbage collector--meaning they shipped as part of the core, despite
// that there was no GUI in R3-Alpha's core open-source release.
//
// !!! Ren-C aims to find a way to wedge GOBs into the user-defined type
// system, where no custom GC behavior is needed.  This would likely involve
// making them more OBJECT!-like, while possibly allowing the series node
// of the object to carry capacity for additional fixed bits in the array
// used for the varlist, without needing another allocation.
//


enum GOB_FLAGS {
    //
    // !!! These were "GOB state flags".  Despite there being only 3 of them,
    // they were previously in a different place than the "GOB flags".
    //
    GOBS_OPEN = 1 << 0, // Window is open
    GOBS_ACTIVE = 1 << 1, // Window is active
    GOBS_NEW = 1 << 2, // Gob is new to pane (old-offset, old-size wrong)

    // These were just generically "GOB flags"
    //
    GOBF_TOP = 1 << 3, // Top level (window or output image)
    GOBF_WINDOW = 1 << 4, // Window (parent is OS window reference)
    GOBF_OPAQUE = 1 << 5, // Has no alpha
    GOBF_STATIC = 1 << 6, // Does not change
    GOBF_HIDDEN = 1 << 7, // Is hidden (e.g. hidden window)
    GOBF_RESIZE = 1 << 8, // Can be resized
    GOBF_NO_TITLE = 1 << 9, // Has window title
    GOBF_NO_BORDER = 1 << 10, // Has no window border
    GOBF_DROPABLE = 1 << 11, // [sic] Let window receive drag and drop
    GOBF_TRANSPARENT = 1 << 12, // Window is in transparent mode
    GOBF_POPUP = 1 << 13, // Window is a popup (with owner window)
    GOBF_MODAL = 1 << 14, // Modal event filtering
    GOBF_ON_TOP = 1 << 15, // The window is always on top
    GOBF_ACTIVE = 1 << 16, // Window is active
    GOBF_MINIMIZE = 1 << 17, // Window is minimized
    GOBF_MAXIMIZE = 1 << 18, // Window is maximized
    GOBF_RESTORE = 1 << 19, // Window is restored
    GOBF_FULLSCREEN = 1 << 20 // Window is fullscreen
};

enum GOB_TYPES {        // Types of content
    GOBT_NONE = 0,
    GOBT_COLOR,
    GOBT_IMAGE,
    GOBT_STRING,
    GOBT_DRAW,
    GOBT_TEXT,
    GOBT_EFFECT,
    GOBT_MAX
};

enum GOB_DTYPES {       // Userdata types
    GOBD_NONE = 0,
    GOBD_OBJECT,
    GOBD_BLOCK,
    GOBD_STRING,
    GOBD_BINARY,
    GOBD_RESV,          // unicode
    GOBD_INTEGER,
    GOBD_MAX
};

#pragma pack(4)

// These packed values for Rebol pairs are "X and Y coordinates" as "F"loat.
// (For PAIR! in Ren-C, actual pairing series are used, which
// can hold two values at full REBVAL precision (either integer or decimal)

typedef struct {
    float x;
    float y;
} REBXYF;


struct rebol_gob {
    union Reb_Header header;

    uint32_t flags; // GOBF_XXX flags and GOBS_XXX state flags

#ifdef REB_DEF
    REBSER *pane;       // List of child GOBs
#else
    void *pane;
#endif

    REBGOB *parent;     // Parent GOB (or window ptr)

    REBYTE alpha;       // transparency
    REBYTE ctype;       // content data type
    REBYTE dtype;       // pointer data type
    REBYTE resv;        // reserved

    REBGOB *owner;      // !!! was a singular item in a union

#ifdef REB_DEF
    REBSER *content;    // content value (block, string, color)
    REBSER *data;       // user defined data
#else
    void *content;
    void *data;
#endif

    REBXYF offset;      // location
    REBXYF size;
    REBXYF old_offset;  // prior location
    REBXYF old_size;    // prior size

#if defined(__LP64__) || defined(__LLP64__)
    //
    // Depending on how the fields are arranged, this may require padding to
    // make sure the REBNOD-derived type is a multiple of 64-bits in size.
    //
#endif
};
#pragma pack()

typedef struct gob_window {             // Maps gob to window
    REBGOB *gob;
    void* win;
    void* compositor;
} REBGOBWINDOWS;

#define GOB_X(g)        ((g)->offset.x)
#define GOB_Y(g)        ((g)->offset.y)
#define GOB_W(g)        ((g)->size.x)
#define GOB_H(g)        ((g)->size.y)

#define GOB_LOG_X(g)        (LOG_COORD_X((g)->offset.x))
#define GOB_LOG_Y(g)        (LOG_COORD_Y((g)->offset.y))
#define GOB_LOG_W(g)        (LOG_COORD_X((g)->size.x))
#define GOB_LOG_H(g)        (LOG_COORD_Y((g)->size.y))

#define GOB_X_INT(g)    ROUND_TO_INT((g)->offset.x)
#define GOB_Y_INT(g)    ROUND_TO_INT((g)->offset.y)
#define GOB_W_INT(g)    ROUND_TO_INT((g)->size.x)
#define GOB_H_INT(g)    ROUND_TO_INT((g)->size.y)

#define GOB_LOG_X_INT(g)    ROUND_TO_INT(LOG_COORD_X((g)->offset.x))
#define GOB_LOG_Y_INT(g)    ROUND_TO_INT(LOG_COORD_Y((g)->offset.y))
#define GOB_LOG_W_INT(g)    ROUND_TO_INT(LOG_COORD_X((g)->size.x))
#define GOB_LOG_H_INT(g)    ROUND_TO_INT(LOG_COORD_Y((g)->size.y))

#define GOB_XO(g)       ((g)->old_offset.x)
#define GOB_YO(g)       ((g)->old_offset.y)
#define GOB_WO(g)       ((g)->old_size.x)
#define GOB_HO(g)       ((g)->old_size.y)
#define GOB_XO_INT(g)   ROUND_TO_INT((g)->old_offset.x)
#define GOB_YO_INT(g)   ROUND_TO_INT((g)->old_offset.y)
#define GOB_WO_INT(g)   ROUND_TO_INT((g)->old_size.x)
#define GOB_HO_INT(g)   ROUND_TO_INT((g)->old_size.y)


#define SET_GOB_FLAG(g,f) \
    cast(void, (g)->flags |= (f))
#define GET_GOB_FLAG(g,f) \
    (did ((g)->flags & (f)))
#define CLR_GOB_FLAG(g,f) \
    cast(void, (g)->flags &= ~(f))


#define GOB_ALPHA(g)        ((g)->alpha)
#define GOB_TYPE(g)         ((g)->ctype)
#define SET_GOB_TYPE(g,t)   ((g)->ctype = (t))
#define GOB_DTYPE(g)        ((g)->dtype)
#define SET_GOB_DTYPE(g,t)  ((g)->dtype = (t))
#define GOB_DATA(g)         ((g)->data)
#define SET_GOB_DATA(g,v)   ((g)->data = (v))
#define GOB_TMP_OWNER(g)    ((g)->owner)

#define IS_GOB_OPAQUE(g)  GET_GOB_FLAG(g, GOBF_OPAQUE)
#define SET_GOB_OPAQUE(g) SET_GOB_FLAG(g, GOBF_OPAQUE)
#define CLR_GOB_OPAQUE(g) CLR_GOB_FLAG(g, GOBF_OPAQUE)

#define GOB_PANE(g)     ((g)->pane)
#define GOB_PARENT(g)   ((g)->parent)
#define GOB_CONTENT(g)  ((g)->content)

#define GOB_STRING(g)       SER_HEAD(GOB_CONTENT(g))
#define GOB_LEN(g)          SER_LEN((g)->pane)
#define SET_GOB_LEN(g,l)    SET_SERIES_LEN((g)->pane, (l))
#define GOB_HEAD(g)         SER_HEAD(REBGOB*, GOB_PANE(g))

#define GOB_BITMAP(g)   GOB_STRING(g)
#define GOB_AT(g,n)   (GOB_HEAD(g)+n)

#define IS_WINDOW(g)    (GOB_PARENT(g) == Gob_Root && GET_GOB_FLAG(g, GOBF_WINDOW))

#define IS_GOB_COLOR(g)  (GOB_TYPE(g) == GOBT_COLOR)
#define IS_GOB_DRAW(g)   (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_DRAW)
#define IS_GOB_IMAGE(g)  (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_IMAGE)
#define IS_GOB_EFFECT(g) (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_EFFECT)
#define IS_GOB_STRING(g) (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_STRING)
#define IS_GOB_TEXT(g)   (GOB_CONTENT(g) && GOB_TYPE(g) == GOBT_TEXT)

extern REBGOB *Gob_Root; // Top level GOB (the screen)
