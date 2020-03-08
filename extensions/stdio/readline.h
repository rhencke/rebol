//
//  File: %readline.h
//  Summary: "Shared Definitions for Windows/POSIX Console Line Reading"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Rebol Open Source Contributors
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
// Windows has a monolithic facility for reading a line of input from the
// user.  This single command call is blocking (also known as "cooked" as
// opposed to "raw") and very limited.  As an initial goal of updating some
// of the very old R3-Alpha input code, the more granular POSIX code for
// implementing a "GNU libreadline"-type facility is being abstracted to
// share pieces of implementation with Windows.
//
// This file will attempt to define the hooks that are shared between the
// Windows and POSIX smart consoles.
//

#include "rebol.h"

// !!! The history mechanism will be disconnected from the line editing
// mechanism--but for the moment, the line editing is the only place we
// get an Init() and Shutdown() opportunity.
//
extern REBVAL *Line_History;  // BLOCK! of TEXT!s

#define READ_BUF_LEN 64   // chars per read()

// The terminal is an opaque type which varies per operating system.  This
// is in C for now, but what it should evolve into is some kind of terminal
// PORT! which would have asynchronous events and behavior.
//
struct Reb_Terminal_Struct;
typedef struct Reb_Terminal_Struct STD_TERM;


extern int Term_Pos(STD_TERM *t);
extern REBVAL *Term_Buffer(STD_TERM *t);

extern STD_TERM *Init_Terminal();

extern void Term_Insert(STD_TERM *t, const REBVAL *v);
extern void Term_Seek(STD_TERM *t, unsigned int pos);
extern void Move_Cursor(STD_TERM *t, int count);
extern void Delete_Char(STD_TERM *t, bool back);
extern void Clear_Line_To_End(STD_TERM *t);

extern void Term_Beep(STD_TERM *t);

extern void Quit_Terminal(STD_TERM *t);

// This is the main workhorse routine that is implemented in both POSIX and
// Windows very differently.  It returns an ANY-VALUE!, not a historical
// R3-Alpha style "EVENT!" (which are too flaky and nebulous to be used
// in this core Ren-C task).
//
extern REBVAL *Try_Get_One_Console_Event(STD_TERM *t, bool buffered);
