//
//  File: %mod-console.c
//  Summary: "Read/Eval/Print Loop (REPL) Skinnable Console for Rebol"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2018 Rebol Open Source Contributors
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

#ifdef TO_WINDOWS

    #undef _WIN32_WINNT  // https://forum.rebol.info/t/326/4
    #define _WIN32_WINNT 0x0501  // Minimum API target: WinXP
    #include <windows.h>

    #undef IS_ERROR  // %windows.h defines this, but so does %sys-core.h

#elif defined(TO_EMSCRIPTEN)
    //
    // Nothing needed here yet...
    //
#else

    #include <signal.h>  // needed for SIGINT, SIGTERM, SIGHUP

#endif


#include "sys-core.h"

#include "tmp-mod-console.h"


extern REB_R Console_Actor(REBFRM *frame_, REBVAL *port, const REBVAL *verb);

//
//  get-console-actor-handle: native [
//
//  {Retrieve handle to the native actor for console}
//
//      return: [handle!]
//  ]
//
REBNATIVE(get_console_actor_handle)
{
    Make_Port_Actor_Handle(D_OUT, &Console_Actor);
    return D_OUT;
}


//=//// USER-INTERRUPT/HALT HANDLING (Ctrl-C, Escape, etc.) ///////////////=//
//
// There's clearly contention for what a user-interrupt key sequence should
// be, given that "Ctrl-C" is copy in GUI applications.  Yet handling escape
// is not necessarily possible on all platforms and situations.
//
// For console applications, we assume that the program starts with user
// interrupting enabled by default...so we have to ask for it not to be when
// it would be bad to have the Rebol stack interrupted--during startup, or
// when in the "kernel" of the host console.
//
// (Note: If halting is done via Ctrl-C, technically it may be set to be
// ignored by a parent process or context, in which case conventional wisdom
// is that we should not be enabling it ourselves.  Review.)
//

bool halting_enabled = true;

#if defined(TO_EMSCRIPTEN)  //=//// EMSCRIPTEN ///////////////////////////=//

// !!! Review how an emscripten console extension should be hooking something
// like a keyboard shortcut for breaking.  With the pthread model, there may
// be shared memory for the GUI to be able to poke a value in that the running
// code can see to perceive a halt.

void Disable_Halting(void) {}
void Enable_Halting(void) {}


#elif defined(TO_WINDOWS)  //=//// WINDOWS ////////////////////////////////=//

// Windows handling is fairly simplistic--this is the callback passed to
// `SetConsoleCtrlHandler()`.  The most annoying thing about cancellation in
// windows is the limited signaling possible in the terminal's readline.
//
BOOL WINAPI Handle_Break(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
      case CTRL_C_EVENT:
      case CTRL_BREAK_EVENT:
        rebHalt();
        return TRUE;  // TRUE = "we handled it"

      case CTRL_CLOSE_EVENT:
        //
        // !!! Theoretically the close event could confirm that the user
        // wants to exit, if there is possible unsaved state.  As a UI
        // premise this is probably less good than persisting the state
        // and bringing it back.
        //
      case CTRL_LOGOFF_EVENT:
      case CTRL_SHUTDOWN_EVENT:
        //
        // They pushed the close button, did a shutdown, etc.  Exit.
        //
        // !!! Review arbitrary "100" exit code here.
        //
        exit(100);

      default:
        return FALSE;  // FALSE = "we didn't handle it"
    }
}

BOOL WINAPI Handle_Nothing(DWORD dwCtrlType)
{
    if (dwCtrlType == CTRL_C_EVENT)
        return TRUE;

    return FALSE;
}

void Disable_Halting(void)
{
    assert(halting_enabled);

    SetConsoleCtrlHandler(Handle_Break, FALSE);
    SetConsoleCtrlHandler(Handle_Nothing, TRUE);

    halting_enabled = false;
}

void Enable_Halting(void)
{
    assert(not halting_enabled);

    SetConsoleCtrlHandler(Handle_Break, TRUE);
    SetConsoleCtrlHandler(Handle_Nothing, FALSE);

    halting_enabled = true;
}

#else  //=//// POSIX, LINUX, MAC, etc. ////////////////////////////////////=//

// SIGINT is the interrupt usually tied to "Ctrl-C".  Note that if you use
// just `signal(SIGINT, Handle_Signal);` as R3-Alpha did, this means that
// blocking read() calls will not be interrupted with EINTR.  One needs to
// use sigaction() if available...it's a slightly newer API.
//
// http://250bpm.com/blog:12
//
// !!! What should be done about SIGTERM ("polite request to end", default
// unix kill) or SIGHUP ("user's terminal disconnected")?  Is it useful to
// register anything for these?  R3-Alpha did, and did the same thing as
// SIGINT.  Not clear why.  It did nothing for SIGQUIT:
//
// SIGQUIT is used to terminate a program in a way that is designed to
// debug it, e.g. a core dump.  Receiving SIGQUIT is a case where
// program exit functions like deletion of temporary files may be
// skipped to provide more state to analyze in a debugging scenario.
//
// SIGKILL is the impolite signal for shutdown; cannot be hooked/blocked

static void Handle_Signal(int sig)
{
    UNUSED(sig);
    rebHalt();
}

struct sigaction old_action;

void Disable_Halting(void)
{
    assert(halting_enabled);

    sigaction(SIGINT, nullptr, &old_action); // fetch current handler
    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = SIG_IGN;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    halting_enabled = false;
}

void Enable_Halting(void)
{
    assert(not halting_enabled);

    if (old_action.sa_handler != SIG_IGN) {
        struct sigaction new_action;
        new_action.sa_handler = &Handle_Signal;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGINT, &new_action, nullptr);
    }

    halting_enabled = true;
}

#endif  //=///////////////////////////////////////////////////////////////=//



// This is called when either the console is running some untrusted skin code
// for its own implementation, or when it wants to execute code on the user's
// behalf.  If the code is on the user's behalf, then any tracing or debug
// hooks will have been enabled before the rebRescue() call invoking this.
//
//
static REBVAL *Run_Sandboxed_Group(REBVAL *group) {
    //
    // DON'T ADD ANY MORE LIBREBOL CODE HERE.  If this is a user-requested
    // evaluation, then any extra libRebol code run here will wind up being
    // shown in a TRACE.  The only thing that's acceptable to see in the
    // backtrace is the GROUP! itself that we are running.  (If we didn't
    // want that, getting rid of it would take some magic).
    //
    // So don't add superfluous libRebol calls here, except to debug.
    //
    return rebQuote(group, rebEND);  // ownership gets proxied
}


//
//  export console: native [
//
//  {Runs an instance of a customizable Read-Eval-Print Loop}
//
//      return: "Integer if QUIT result, path if RESUME instruction"
//          [integer! path!]
//      /provoke "Give the console some code to run before taking user input"
//      provocation "Block must return a console state, group is cancellable"
//          [block! group!]
//      /resumable "Allow RESUME instruction (will return a PATH!)"
//  ]
//
REBNATIVE(console)
//
// !!! The idea behind the console is that it can be called with skinning;
// so that if BREAKPOINT wants to spin up a console, it can...but with a
// little bit of injected information like telling you the current stack
// level it's focused on.  How that's going to work is still pretty up
// in the air.
//
// What it will return will be either an exit code (INTEGER!), a signal for
// cancellation (BLANK!), or a debugging instruction (BLOCK!).
{
    CONSOLE_INCLUDE_PARAMS_OF_CONSOLE;

    // We only enable halting (e.g. Ctrl-C, or Escape, or whatever) when user
    // code is running...not when the HOST-CONSOLE function itself is, or
    // during startup.  (Enabling it during startup would require a special
    // "kill" mode that did not call rebHalt(), as basic startup cannot
    // meaningfully be halted--the system would be in an incomplete state.)
    //
    bool was_halting_enabled = halting_enabled;
    if (was_halting_enabled)
        Disable_Halting();

    // The DO and APPLY hooks are used to implement things like tracing
    // or debugging.  If they were allowed to run during the host
    // console, they would create a fair amount of havoc (the console
    // is supposed to be "invisible" and not show up on the stack...as if
    // it were part of the C codebase, even though it isn't written in C)
    //
    REBEVL saved_eval_hook = PG_Eval_Maybe_Stale_Throws;
    REBNAT saved_dispatch_hook = PG_Dispatch;

    // !!! While the new mode of TRACE (and other code hooking function
    // execution) is covered by `saved_eval_hook/saved_apply_hook`, there
    // is independent tracing code in PARSE which is also enabled by TRACE ON
    // and has to be silenced during console-related code.  Review how hooks
    // into PARSE and other services can be avoided by the console itself
    //
    REBINT Save_Trace_Level = Trace_Level;
    REBINT Save_Trace_Depth = Trace_Depth;

    REBVAL *result = nullptr;
    bool no_recover = false;  // allow one try at HOST-CONSOLE internal error

    REBVAL *code;
    if (REF(provoke)) {
        code = rebArg("provocation", rebEND);  // fetch as an API handle
        UNUSED(ARG(provocation));
        goto provoked;
    }
    else {
        code = rebBlank();
        result = rebBlank();
    }

    while (true) {
       assert(not halting_enabled);  // not while HOST-CONSOLE is on the stack

      recover: ;  // Note: semicolon needed as next statement is declaration

        // This runs the HOST-CONSOLE, which returns *requests* to execute
        // arbitrary code by way of its return results.  The ENTRAP is thus
        // here to intercept bugs *in HOST-CONSOLE itself*.  Any evaluations
        // for the user (or on behalf of the console skin) are done in
        // Run_Sandboxed_Group().
        //
        REBVAL *trapped;  // Note: goto would cross initialization
        trapped = rebRunQ(
            "entrap [",
                "ext-console-impl",  // action! that takes 2 args, run it
                code,  // group! or block! executed prior (or blank!)
                result,  // prior result quoted, or error (or blank!)
                rebL(REF(resumable)),
            "]", rebEND
        );

        rebRelease(code);
        rebRelease(result);

        if (rebDidQ("error?", trapped, rebEND)) {
            //
            // If the HOST-CONSOLE function has any of its own implementation
            // that could raise an error (or act as an uncaught throw) it
            // *should* be returned as a BLOCK!.  This way the "console skin"
            // can be reset to the default.  If HOST-CONSOLE itself fails
            // (e.g. a typo in the implementation) there's probably not much
            // use in trying again...but give it a chance rather than just
            // crash.  Pass it back something that looks like an instruction
            // it might have generated (a BLOCK!) asking itself to crash.

            if (no_recover)
                rebJumpsQ("PANIC", trapped, rebEND);

            code = rebRunQ("[#host-console-error]", rebEND);
            result = trapped;
            no_recover = true;  // no second chances until user code runs
            goto recover;
        }

        code = rebRunQ("first", trapped, rebEND);  // entrap []'s the output
        rebRelease(trapped); // don't need the outer block any more

      provoked:

        if (rebDidQ("integer?", code, rebEND))
            break;  // when HOST-CONSOLE returns INTEGER! it means exit code

        if (rebDidQ("path?", code, rebEND)) {
            assert(REF(resumable));
            break;
        }

        bool is_console_instruction = rebDidQ("block?", code, rebEND);
        REBVAL *group;

        if (is_console_instruction) {
            group = rebRunQ("as group!", code, rebEND);  // to run without DO
        }
        else {
            group = rebRunQ(code, rebEND);  // rebRelease() w/o affecting code

            // If they made it to a user mode instruction, the console skin
            // must not be broken beyond all repair.  So re-enable recovery.
            //
            no_recover = false;

            // Restore custom DO and APPLY hooks, but only if it was a GROUP!
            // initially (indicating running code initiated by the user).
            //
            // (We do not want to trace/debug/instrument Rebol code that the
            // console is using to implement *itself*, which it does with
            // BLOCK! Same for Trace_Level seen by PARSE.
            //
            PG_Eval_Maybe_Stale_Throws = saved_eval_hook;
            PG_Dispatch = saved_dispatch_hook;
            Trace_Level = Save_Trace_Level;
            Trace_Depth = Save_Trace_Depth;
        }

        // Both console-initiated and user-initiated code is cancellable with
        // Ctrl-C (though it's up to HOST-CONSOLE on the next iteration to
        // decide whether to accept the cancellation or consider it an error
        // condition or a reason to fall back to the default skin).
        //
        Enable_Halting();
        result = rebRescue(cast(REBDNG*, &Run_Sandboxed_Group), group);
        rebRelease(group);  // Note: does not release `code`
        Disable_Halting();

        // If the custom DO and APPLY hooks were changed by the user code,
        // then save them...but restore the unhooked versions for the next
        // iteration of HOST-CONSOLE.  Same for Trace_Level seen by PARSE.
        //
        if (not is_console_instruction) {
            saved_eval_hook = PG_Eval_Maybe_Stale_Throws;
            saved_dispatch_hook = PG_Dispatch;
            PG_Eval_Maybe_Stale_Throws = &Eval_Internal_Maybe_Stale_Throws;
            PG_Dispatch = &Dispatch_Internal;
            Save_Trace_Level = Trace_Level;
            Save_Trace_Depth = Trace_Depth;
            Trace_Level = 0;
            Trace_Depth = 0;
        }
    }

    // Exit code is now an INTEGER! or a resume instruction PATH!

    if (was_halting_enabled)
        Enable_Halting();

    return code;  // http://stackoverflow.com/q/1101957/
}


// Index values for the properties in a "resume instruction" (see notes on
// REBNATIVE(resume))
//
enum {
    RESUME_INST_MODE = 0,   // FALSE if /WITH, TRUE if /DO, BLANK! if default
    RESUME_INST_PAYLOAD,    // code block to /DO or value of /WITH
    RESUME_INST_TARGET,     // unwind target, BLANK! to return from breakpoint
    RESUME_INST_MAX
};


//
//  export resume: native [
//
//  {Resume after a breakpoint, can evaluate code in the breaking context.}
//
//      /with
//          "Return the given value as return value from BREAKPOINT"
//      value [any-value!]
//          "Value to use"
//      /do
//          "Evaluate given code as return value from BREAKPOINT"
//      code [block!]
//          "Code to evaluate"
//  ]
//
REBNATIVE(resume)
//
// The CONSOLE makes a wall to prevent arbitrary THROWs and FAILs from ending
// a level of interactive inspection.  But RESUME is special, and makes a very
// specific instruction (with a throw /NAME of the RESUME native) to signal a
// desire to end the interactive session.
//
// When the BREAKPOINT native gets control back from CONSOLE, it interprets
// and executes the instruction.  This offers the additional benefit that
// each host doesn't have to rewrite interpretation in the hook--they only
// need to recognize a RESUME throw and pass the argument back.
//
// !!! Initially, this supported /AT:
//
//      /at
//          "Return from another call up stack besides the breakpoint"
//      level [frame! action! integer!]
//          "Stack level to target in unwinding (can be BACKTRACE #)"
//
// While an interesting feature, it's not currently a priority.
{
    CONSOLE_INCLUDE_PARAMS_OF_RESUME;

    if (REF(with) && REF(do)) {
        //
        // /WITH and /DO both dictate a default return result, (/DO evaluates
        // and /WITH does not)  They are mutually exclusive.
        //
        fail (Error_Bad_Refines_Raw());
    }

    // We don't actually want to run the code for a /DO here.  If we tried
    // to run code from this stack level--and it failed or threw without
    // some special protocol--we'd stay stuck in the breakpoint's sandbox.
    //
    // The /DO code we received needs to actually be run by the host's
    // breakpoint hook, once it knows that non-local jumps to above the break
    // level (throws, returns, fails) actually intended to be "resuming".

    REBARR *instruction = Make_Array(RESUME_INST_MAX);

    if (REF(with)) {
        Init_False(ARR_AT(instruction, RESUME_INST_MODE)); // don't DO
        Move_Value(ARR_AT(instruction, RESUME_INST_PAYLOAD), ARG(value));
    }
    else if (REF(do)) {
        Init_True(ARR_AT(instruction, RESUME_INST_MODE)); // DO value
        Move_Value(ARR_AT(instruction, RESUME_INST_PAYLOAD), ARG(code));
    }
    else {
        Init_Blank(ARR_AT(instruction, RESUME_INST_MODE)); // use default
        Init_Blank(ARR_AT(instruction, RESUME_INST_PAYLOAD));
    }

    // For /AT feature, currently not supported
    //
    Init_Blank(ARR_AT(instruction, RESUME_INST_TARGET));

    TERM_ARRAY_LEN(instruction, RESUME_INST_MAX);

    // We put the resume instruction into a PATH! just to make it a little
    // bit more unusual than a BLOCK!.  More hardened approaches might put
    // a special symbol as a "magic number" or somehow version the protocol,
    // but for now we'll assume that the only decoder is BREAKPOINT and it
    // will be kept in sync.
    //
    DECLARE_LOCAL (cell);
    Init_Path(cell, instruction);

    // Throw the instruction with the name of the RESUME function.  (Note:
    // there is no NAT_VALUE() for extensions, yet)
    //
    Init_Action_Maybe_Bound(D_OUT, FRM_PHASE(frame_), FRM_BINDING(frame_));
    return Init_Thrown_With_Label(D_OUT, cell, D_OUT);
}
