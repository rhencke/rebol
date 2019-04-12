//
//  File: %host-device.c
//  Summary: "Device management and command dispatch"
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
// OS independent
//
// This module is parsed for function declarations used to
// build prototypes, tables, and other definitions. To change
// function arguments requires a rebuild of the REBOL library.
//
// This module implements a device management system for
// REBOL devices and tracking their I/O requests.
// It is intentionally kept very simple (makes debugging easy!)
//
// 1. Not a lot of devices are needed (dozens, not hundreds).
// 2. Devices are referenced by integer (index into device table).
// 3. A single device can support multiple requests.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "reb-host.h"


// REBOL "DEVICES"
//
// !!! The devices are no longer a table, but a linked list.  The polling
// priority is in the order the list is in.  If there's going to be some kind
// of priority scheme, it would have to be added to the API for registering.
//
REBDEV *Devices;


static int Poll_Default(REBDEV *dev)
{
    // The default polling function for devices.
    // Retries pending requests. Return TRUE if status changed.

    bool change = false;

    REBREQ **prior = &dev->pending;
    REBREQ *req;
    for (req = *prior; req; req = *prior) {
        assert(Req(req)->command < RDC_MAX);

        // Call command again:

        Req(req)->flags &= ~RRF_ACTIVE;
        int result = dev->commands[Req(req)->command](req);

        if (result == DR_DONE) { // if done, remove from pending list
            *prior = NextReq(req);
            NextReq(req) = nullptr;
            Req(req)->flags &= ~RRF_PENDING;
            change = true;
        }
        else {
            assert(result == DR_PEND);

            prior = &NextReq(req);
            if (Req(req)->flags & RRF_ACTIVE)
                change = true;
        }
    }

    return change ? 1 : 0;
}


//
//  Attach_Request: C
//
// Attach a request to a device's pending or accept list.
// Node is a pointer to the head pointer of the req list.
//
void Attach_Request(REBREQ **node, REBREQ *req)
{
    REBREQ *r;

    // See if its there, and get last req:
    for (r = *node; r; r = *node) {
        if (r == req) return; // already in list
        node = &NextReq(r);
    }

    // Link the new request to end:
    *node = req;
    Ensure_Req_Managed(req);
    NextReq(req) = nullptr;
    Req(req)->flags |= RRF_PENDING;
}


//
//  Detach_Request: C
//
// Detach a request to a device's pending or accept list.
// If it is not in list, then no harm done.
//
void Detach_Request(REBREQ **node, REBREQ *req)
{
    REBREQ *r;

    for (r = *node; r; r = *node) {
        if (r == req) {
            *node = NextReq(req);
            NextReq(req) = nullptr;
            Req(req)->flags |= RRF_PENDING;
            return;
        }
        node = &NextReq(r);
    }
}


// For use with rebRescue(), to intercept failures in order to do some
// processing if necessary before passing the failure up the stack.  The
// rescue will return this function's result (an INTEGER!) if no error is
// raised during the device code.
//
static REBVAL *Dangerous_Command(REBREQ *req) {
    REBDEV *dev = Req(req)->device;

    int result = (dev->commands[Req(req)->command])(req);
    return rebInteger(result);
}


//
//  OS_Do_Device: C
//
// Tell a device to perform a command.  Non-blocking in many cases and will
// attach the request for polling.
//
// !!! R3-Alpha returned 0 for success (DR_DONE), 1 for command still pending
// (DR_PEND) and negative numbers for errors.  As the device model is revamped
// the concept is to return the actual result, NULL if pending, or an ERROR!.
//
REBVAL *OS_Do_Device(REBREQ *req, enum Reb_Device_Command command)
{
    Req(req)->command = command;

    REBDEV *dev = Req(req)->device;
    if (dev == NULL)
        rebJumps("FAIL {Rebol Device Not Found}", rebEND);

    if (not (dev->flags & RDF_INIT)) {
        if (dev->flags & RDO_MUST_INIT)
            rebJumps("FAIL {Rebol Device Uninitialized}", rebEND);

        if (
            !dev->commands[RDC_INIT]
            || !dev->commands[RDC_INIT](cast(REBREQ*, dev))
        ){
            dev->flags |= RDF_INIT;
        }
    }

    if (dev->commands[Req(req)->command] == NULL)
        rebJumps("FAIL {Invalid Command for Rebol Device}", rebEND);

    // !!! Currently the StdIO port is initialized before Rebol's startup
    // code ever runs.  This is to allow debug messages to be printed during
    // boot.  That means it's too early to be pushing traps, having errors,
    // or really using any REBVALs at all.  Review the dependency, but in
    // the meantime just don't try and push trapping of errors if there's
    // not at least one Rebol state pushed.
    //
    if (Req(req)->device == &Dev_StdIO and Req(req)->command == RDC_OPEN) {
        int result = (dev->commands[Req(req)->command])(req);
        assert(result == DR_DONE);
        UNUSED(result);
        return NULL;
    }

    // !!! R3-Alpha had it so when an error was raised from a "device request"
    // it would give back DR_ERROR and the caller would have to interpret an
    // integer error code that was filled into the request.  Sometimes these
    // were OS-specific, and hence not readable to most people...and sometimes
    // they were just plain made up (e.g. search for `req->error = -18` in the
    // R3-Alpha sources.)
    //
    // The plan here is to use the fail() mechanic to let literate error
    // messages be produced.  However, there was code here that would react
    // to DR_ERROR in order to allow for cleanup in the case that a request
    // was flagged with RRF_ALLOC.  New lifetime management strategies that
    // attach storage to stack frames should make that aspect obsolete.
    //
    // There was one other aspect of presumed pending removal, however.  For
    // now, preserve that behavior by always running the device code with
    // a trap in effect.

    REBVAL *error_or_int = rebRescue(cast(REBDNG*, &Dangerous_Command), req);

    if (rebDid("error?", error_or_int, rebEND)) {
        if (dev->pending)
            Detach_Request(&dev->pending, req); // "often a no-op", it said

        return error_or_int;

        // !!! Should an auto-fail variation be offered, for callers who
        // do not want to get involved?
    }

    int result = rebUnboxInteger(rebR(error_or_int), rebEND);

    // If request is pending, attach it to device for polling:
    //
    if (result == DR_PEND) {
        Attach_Request(&dev->pending, req);
        return NULL;
    }

    assert(result == DR_DONE);
    if (dev->pending)
        Detach_Request(&dev->pending, req); // often a no-op

    return rebLogic(true);
}


//
//  OS_Do_Device_Sync: C
//
// Convenience routine that wraps OS_DO_DEVICE for simple requests.
//
// !!! Because the device layer is deprecated, the relevant inelegance of
// this is not particularly important...more important is that the API
// handles and error mechanism works.
//
void OS_Do_Device_Sync(REBREQ *req, enum Reb_Device_Command command)
{
    REBVAL *result = OS_DO_DEVICE(req, command);
    assert(result != NULL); // should be synchronous
    if (rebDid("error?", result, rebEND))
        rebJumps("FAIL", result, rebEND);
    rebRelease(result); // ignore result
}


//
//  OS_Make_Devreq: C
//
REBREQ *OS_Make_Devreq(REBDEV *device)  // rebdev
{
    return cast(REBREQ*, rebMake_Rebreq(device));
}


//
//  OS_Abort_Device: C
//
// Ask device to abort prior request.
//
int OS_Abort_Device(REBREQ *req)
{
    REBDEV *dev = Req(req)->device;
    assert(dev != NULL);

    Detach_Request(&dev->pending, req);
    return 0;
}


//
//  OS_Poll_Devices: C
//
// Poll devices for activity.
//
// Returns count of devices that changed status.
//
// Devices with pending lists will be called to see if
// there is a change in status of those requests. If so,
// those devices are allowed to change the state of those
// requests or call-back into special REBOL functions
// (e.g. Add_Event for GUI) to invoke special actions.
//
int OS_Poll_Devices(void)
{
    int num_changed = 0;

    REBDEV *dev = Devices;
    for (; dev != nullptr; dev = dev->next) {
        if (Poll_Default(dev))
            ++num_changed;
    }

    return num_changed;
}


//
//  OS_Quit_Devices: C
//
// Terminate all devices in preparation to quit.
//
// Allows devices to perform cleanup and resource freeing.
//
// Set flags to zero for now. (May later be used to indicate
// a device query check or a brute force quit.)
//
// Returns: 0 for now.
//
int OS_Quit_Devices(int flags)
{
    UNUSED(flags);

    REBDEV *dev = Devices;
    for (; dev != nullptr; dev = dev->next) {
        if (not (dev->flags & RDF_INIT))
            continue;

        if (dev->commands[RDC_QUIT] == nullptr)
            continue;

        dev->commands[RDC_QUIT](cast(REBREQ*, dev));
    }

    return 0;
}


//
//  OS_Register_Device: C
//
// !!! This follows the R3-Alpha model that a device is expected to be a
// global static variable, that is registered until the program finishes.  A
// more dynamic solution would be needed for DLLs that unload and reload...
// because the memory for the device would "go missing"--hence it would need
// some mechanism of unregistering.
//
void OS_Register_Device(REBDEV *dev) {
    dev->next = Devices;
    Devices = dev;
}


//
//  OS_Wait: C
//
// Check if devices need attention, and if not, then wait.
// The wait can be interrupted by a GUI event, otherwise
// the timeout will wake it.
//
// Res specifies resolution. (No wait if less than this.)
//
// Returns:
//     -1: Devices have changed state.
//      0: past given millsecs
//      1: wait in timer
//
// The time it takes for the devices to be scanned is
// subtracted from the timer value.
//
int OS_Wait(unsigned int millisec, unsigned int res)
{
    // printf("OS_Wait %d\n", millisec);

    int64_t base = OS_DELTA_TIME(0); // start timing

    // !!! The request is created here due to a comment that said "setup for
    // timing" and said it was okay to stack allocate it because "QUERY
    // below does not store it".  Having eliminated stack-allocated REBREQ,
    // it's not clear if it makes sense to allocate it here vs. below.
    //
    REBREQ *req = OS_Make_Devreq(&Dev_Event);

    OS_REAP_PROCESS(-1, NULL, 0);

    // Let any pending device I/O have a chance to run:
    //
    if (OS_Poll_Devices()) {
        Free_Req(req);
        return -1;
    }

    // Nothing, so wait for period of time

    unsigned int delta = OS_DELTA_TIME(base) / 1000 + res;
    if (delta >= millisec) {
        Free_Req(req);
        return 0;
    }

    millisec -= delta; // account for time lost above
    Req(req)->length = millisec;

    // printf("Wait: %d ms\n", millisec);

    // Comment said "wait for timer or other event"
    //
    OS_DO_DEVICE_SYNC(req, RDC_QUERY);

    Free_Req(req);

    return 1;  // layer above should check delta again
}
