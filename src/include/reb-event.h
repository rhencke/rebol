//
//  File: %reb-event.h
//  Summary: "REBOL event definitions"
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
// !!! The R3-Alpha host model and eventing system is generally deprecated
// in Ren-C, but is being kept working due to dependencies for R3/View.
//
// One change that was necessary in Ren-C was for payloads inside of REBVALs
// to be split into a 64-bit aligned portion, and a common 32-bit "extra"
// portion that would be 32-bit aligned on 32-bit platforms.  This change
// was needed in order to write a common member of a union without
// disengaging the rest of the payload.
//
// That required the Reb_Event--which was previously three 32-bit quantities,
// to split its payload up.  Now to get a complete event structure through
// the API, a full alias to a REBVAL is given.
//
// EVENT PAYLOAD CONTAINS 2 POINTER-SIZED THINGS
//
//     "eventee": REBREQ* (for device events) or REBSER* (port or object)
//     "data": 32-bit quantity "an x/y position or keycode (raw/decoded)"
//
// EVENT EXTRA CONTAINS 4 BYTES
//
//     uint8_t type;   // event id (mouse-move, mouse-button, etc)
//     uint8_t flags;  // special flags
//     uint8_t win;    // window id
//     uint8_t model;  // port, object, gui, callback


#define REBEVT REBVAL


// Special event flags:
//
// !!! So long as events are directly hooking into the low-level REBVAL
// implementation, this could just use EVENT_FLAG_XXX flags.  eventee could
// be a binding to a REBNOD that was able to inspect that node to get the
// data "model".  

enum {
    EVF_COPIED = 1 << 0, // event data has been copied
    EVF_HAS_XY = 1 << 1, // map-event will work on it
    EVF_DOUBLE = 1 << 2, // double click detected
    EVF_CONTROL = 1 << 3,
    EVF_SHIFT = 1 << 4
};


// Event port data model

enum {
    EVM_DEVICE,     // I/O request holds the port pointer
    EVM_PORT,       // event holds port pointer
    EVM_OBJECT,     // event holds object context pointer
    EVM_GUI,        // GUI event uses system/view/event/port
    EVM_CALLBACK,   // Callback event uses system/ports/callback port
    EVM_MAX
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  EVENT!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's events are used for the GUI and for network and I/O.  They are
// essentially just a union of some structures which are packed so they can
// fit into a REBVAL's payload size.
//
// The available event models are:
//
// * EVM_PORT
// * EVM_OBJECT
// * EVM_DEVICE
// * EVM_CALLBACK
// * EVM_GUI
//

#define VAL_EVENT_TYPE(v) \
    EXTRA(Bytes, (v)).common[0]

#define VAL_EVENT_FLAGS(v) \
    EXTRA(Bytes, (v)).common[1]

#define VAL_EVENT_WIN(v) \
    EXTRA(Bytes, (v)).common[2]

#define VAL_EVENT_MODEL(v) \
    EXTRA(Bytes, (v)).common[3]

#define VAL_EVENT_REQ(v) \
    cast(REBREQ*, PAYLOAD(Custom, (v)).first.p)

#define VAL_EVENT_SER(v) \
    cast(REBSER*, PAYLOAD(Custom, (v)).first.p)

#define mutable_VAL_EVENT_REQ(v) \
    *cast(REBREQ**, &PAYLOAD(Custom, (v)).first.p)

#define mutable_VAL_EVENT_SER(v) \
    *cast(REBSER**, &PAYLOAD(Custom, (v)).first.p)

#define VAL_EVENT_DATA(v) \
    PAYLOAD(Custom, (v)).second.u

#define IS_EVENT_MODEL(v,f) \
    (VAL_EVENT_MODEL(v) == (f))

inline static void SET_EVENT_INFO(
    RELVAL *val,
    uint8_t type,
    uint8_t flags,
    uint8_t win
){
    VAL_EVENT_TYPE(val) = type;
    VAL_EVENT_FLAGS(val) = flags;
    VAL_EVENT_WIN(val) = win;
}

// Position event data

#define VAL_EVENT_X(v) \
    cast(REBINT, cast(short, VAL_EVENT_DATA(v) & 0xffff))

#define VAL_EVENT_Y(v) \
    cast(REBINT, cast(short, (VAL_EVENT_DATA(v) >> 16) & 0xffff))

#define VAL_EVENT_XY(v) \
    (VAL_EVENT_DATA(v))

inline static void SET_EVENT_XY(RELVAL *v, REBINT x, REBINT y) {
    //
    // !!! "conversion to u32 from REBINT may change the sign of the result"
    // Hence cast.  Not clear what the intent is.
    //
    VAL_EVENT_DATA(v) = cast(uint32_t, ((y << 16) | (x & 0xffff)));
}

// Key event data

#define VAL_EVENT_KEY(v) \
    (VAL_EVENT_DATA(v) & 0xffff)

#define VAL_EVENT_KCODE(v) \
    ((VAL_EVENT_DATA(v) >> 16) & 0xffff)

inline static void SET_EVENT_KEY(RELVAL *v, REBCNT k, REBCNT c) {
    VAL_EVENT_DATA(v) = ((c << 16) + k);
}
