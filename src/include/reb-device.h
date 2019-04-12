//
//  File: %reb-device.h
//  Summary: "External REBOL Devices (OS Independent)"
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
// Critical: all struct alignment must be 4 bytes (see compile options)
//

#include <assert.h>
#include "assert-fixes.h"


enum Reb_Device_Command {
    RDC_INIT,       // init device driver resources
    RDC_QUIT,       // cleanup device driver resources

    RDC_OPEN,       // open device unit (port)
    RDC_CLOSE,      // close device unit

    RDC_READ,       // read from unit
    RDC_WRITE,      // write to unit

    RDC_CONNECT,    // connect (in or out)

    RDC_QUERY,      // query unit info
    RDC_MODIFY,     // set modes (also get modes)

    RDC_CREATE,     // create unit target
    RDC_DELETE,     // delete unit target
    RDC_RENAME,
    RDC_LOOKUP,
    RDC_MAX
};

// Device Request (Command) Return Codes:
#define DR_PEND   1 // request is still pending
#define DR_DONE   0 // request is complete w/o errors

// REBOL Device Flags and Options (bitnums):
enum {
    // Status flags:
    RDF_INIT = 1 << 0, // Device is initialized
    RDF_OPEN = 1 << 1, // Global open (for devs that cannot multi-open)
    // Options:
    RDO_MUST_INIT = 1 << 2 // Do not allow auto init (manual init required)

    // !!! There used to be something here called "RDO_AUTO_POLL" which said
    // "Poll device, even if no requests (e.g. interrupts)".  There were no
    // instances.  If someone needed to accomplish this, they could just put
    // in a request that never says it's done, but keeps asking to be left
    // in the pending queue.
};

// REBOL Request Flags (bitnums):
enum {
    RRF_OPEN = 1 << 0, // Port is open
    RRF_DONE = 1 << 1, // Request is done (used when extern proc changes it)
    RRF_FLUSH = 1 << 2, // Flush WRITE
//  RRF_PREWAKE,    // C-callback before awake happens (to update port object)
    RRF_PENDING = 1 << 3, // Request is attached to pending list
    RRF_ACTIVE = 1 << 5, // Port is active, even no new events yet

    // !!! This was a "local flag to mark null device" which when not managed
    // here was confusing.  Given the need to essentially replace the whole
    // device model, it's clearer to keep it here.
    //
    SF_DEV_NULL = 1 << 16
};


// RFM - REBOL File Modes
enum {
    RFM_READ = 1 << 0,
    RFM_WRITE = 1 << 1,
    RFM_APPEND = 1 << 2,
    RFM_SEEK = 1 << 3,
    RFM_NEW = 1 << 4,
    RFM_READONLY = 1 << 5,
    RFM_TRUNCATE = 1 << 6,
    RFM_RESEEK = 1 << 7, // file index has moved, reseek
    RFM_DIR = 1 << 8,
    RFM_TEXT = 1 << 9 // on appropriate platforms, translate LF to CR LF
};

#define MAX_FILE_NAME 1022

enum {
    RDM_NULL = 1 << 0 // !!! "Null device", can this just be a boolean?
};

// Serial Parity
enum {
    SERIAL_PARITY_NONE,
    SERIAL_PARITY_ODD,
    SERIAL_PARITY_EVEN
};

// Serial Flow Control
enum {
    SERIAL_FLOW_CONTROL_NONE,
    SERIAL_FLOW_CONTROL_HARDWARE,
    SERIAL_FLOW_CONTROL_SOFTWARE
};

// Forward references:
typedef struct rebol_device REBDEV;

// Commands:
typedef int32_t (*DEVICE_CMD_CFUNC)(REBREQ *req);
#define DEVICE_CMD int32_t // Used to define

// Device structure:
struct rebol_device {
    const char *title;      // title of device
    uint32_t version;       // version, revision, release
    uint32_t date;          // year, month, day, hour
    DEVICE_CMD_CFUNC *commands; // command dispatch table
    uint32_t max_command;   // keep commands in bounds
    uint32_t req_size;      // size of the request state
    REBREQ *pending;        // pending requests
    uint32_t flags;         // state: open, signal

    REBDEV *next;  // next in linked list of registered devices
};

// Inializer (keep ordered same as above)
#define DEFINE_DEV(w,t,v,c,m,s) \
    REBDEV w = {t, v, 0, c, m, s, 0, 0, 0}

// Request structure:       // Allowed to be extended by some devices
struct rebol_devreq {

    REBDEV *device;
    union {
        void *handle;       // OS object
        int socket;         // OS identifier
        int id;
    } requestee;

    enum Reb_Device_Command command;  // command code

    uint32_t modes;         // special modes, types or attributes
    uint16_t flags;         // request flags
    uint16_t state;         // device process flags
    int32_t timeout;        // request timeout
//  int (*prewake)(void *); // callback before awake

    // !!! Only one of these fields is active at a time, so what it really
    // represents is a union.  A struct helps catch errors while it is being
    // untangled.  Ultimately what this would evolve into would just be a
    // REBVAL*, as this becomes a more Rebol-aware and "usermode" concept.
    //
    struct {
        unsigned char *data;
        REBVAL *binary;  // !!! outlives the rebreq (on stack or in port_ctx)
    } common;
    uint32_t length;        // length to transfer
    uint32_t actual;        // length actually transferred
};

#define Req(req) \
    cast(struct rebol_devreq*, rebReq(req))  // !!! Transitional hack

#define NextReq(req) \
    *cast(REBREQ**, rebAddrOfNextReq(req))  // !!! Transitional hack

#define Ensure_Req_Managed(req) \
    rebEnsure_Req_Managed(req)  // !!! Transitional hack

#define Free_Req(req) \
    rebFree_Req(req)  // !!! Transitional hack

#define ReqPortCtx(req) \
    *cast(void**, rebAddrOfReqPortCtx(req))  // !!! Transitional hack


// !!! These devices will all be moved to extensions, so that the core
// evaluator does not need to be linked to the R3-Alpha device model.

EXTERN_C REBDEV Dev_StdIO;
