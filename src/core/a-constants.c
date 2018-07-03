//
//  File: %a-constants.c
//  Summary: "Special global constants, scanned to make %tmp-constants.h"
//  Section: environment
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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
// Most text strings in Rebol should appear in the bootstrap files as Rebol
// code.  This allows for "internationalization" without needing to update
// the C code.  Other advantages are that the strings are compressed,
// "reduces tampering", etc.
//
// So to keep track of any stray English strings in the executable which make
// it into the user's view, they should be located here.
//
// NOTE: It's acceptable for hardcoded English strings to appear in the debug
// build or in other debug settings, as anyone working with the C code itself
// is basically expected to be able to read English (given the variable names
// and comments in the C are English).
//
// NOTE: For a constant to be picked up from this file, the parse rule is
// that it !!HAS TO START WITH `const`!!.  It makes the extern definition
// based on what it captures up to the `=` sign.
//

#include "reb-c.h"
#define REB_DEF
#include "reb-defs.h"
#include "tmp-constants.h" // need the extern definitions

const char Str_REBOL[] = "REBOL";

// A panic() indicates a serious malfunction, and should not make use of
// Rebol-structured error message delivery in the release build.

const char Str_Panic_Title[] = "Rebol Internal Error";

const char Str_Panic_Directions[] = {
    "If you need to file a bug in the issue tracker, please give thorough\n"
    "details on how to reproduce the problem:\n"
    "\n"
    "    https://github.com/metaeducation/ren-c/issues\n"
    "\n"
    "Include the following information in the report:\n\n"
};

const char * Hex_Digits = "0123456789ABCDEF";

const char * const Esc_Names[] = {
    // Must match enum REBOL_Esc_Codes!
    "line",
    "tab",
    "page",
    "escape",
    "esc",
    "back",
    "del",
    "null"
};

const REBYTE Esc_Codes[] = {
    // Must match enum REBOL_Esc_Codes!
    10,     // line
    9,      // tab
    12,     // page
    27,     // escape
    27,     // esc
    8,      // back
    127,    // del
    0       // null
};

// Zen Point on naming cues: was "Month_Lengths", but said 29 for Feb! --@HF
const REBYTE Month_Max_Days[12] = {
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const char * const Month_Names[12] = {
    "January", "February", "March", "April", "May", "June", "July", "August",
    "September", "October", "November", "December"
};


// Used by scanner. Keep in sync with enum Reb_Token in %scan.h file!
//
const char * const Token_Names[] = {
    "end-of-script",
    "newline",
    "block-end",
    "group-end",
    "word",
    "set",
    "get",
    "lit",
    "blank",
    "bar",
    "lit-bar",
    "logic",
    "integer",
    "decimal",
    "percent",
    "money",
    "time",
    "date",
    "char",
    "block-begin",
    "group-begin",
    "string",
    "binary",
    "pair",
    "tuple",
    "file",
    "email",
    "url",
    "issue",
    "tag",
    "path",
    "refine",
    "construct",
    NULL
};


// !!! For now, (R)ebol (M)essages use the historical Debug_Fmt() output
// method, which is basically like `printf()`.  Over the long term, they
// should use declarations like the (R)ebol (E)rrors do with RE_XXX values
// loaded during boot.
//
// The goal should be that any non-debug-build only strings mentioned from C
// that can be seen in the course of normal operation should go through this
// abstraction.  Ultimately that would permit internationalization, and the
// benefit of not needing to ship a release build binary with a string-based
// format dialect.
//
// Switching strings to use this convention should ultimately parallel the
// `Error()` generation, where the arguments are Rebol values and not C
// raw memory as parameters.  Debug_Fmt() should also just be changed to
// a normal `Print()` naming.
//
const char RM_ERROR_LABEL[] = "Error: ";
const char RM_BAD_ERROR_FORMAT[] = "(improperly formatted error)";
const char RM_ERROR_WHERE[] = "** Where: ";
const char RM_ERROR_NEAR[] = "** Near: ";
const char RM_ERROR_FILE[] = "** File: ";
const char RM_ERROR_LINE[] = "** Line: ";

const char RM_WATCH_RECYCLE[] = "RECYCLE: %d series";

const char RM_TRACE_FUNCTION[] = "--> %s";
const char RM_TRACE_RETURN[] = "<-- %s == ";
const char RM_TRACE_ERROR[] = "**: error : %r %r";

const char RM_TRACE_PARSE_VALUE[] = "Parse %s: %r";
const char RM_TRACE_PARSE_INPUT[] = "Parse input: %s";

// The return result from a native dispatcher leverages the fact that bit
// patterns for valid UTF-8 and valid cell headers do not overlap.  This means
// it's possible to have a return result be an enumerated type -or- a pointer
// to a cell in the same pointer value.
//
// Hence, an arbitrary cell pointer may be returned from a native--in which
// case it will be checked to see if it is thrown and processed if it is, or
// checked to see if it's an unmanaged API handle and released if it is...
// ultimately putting the cell into f->out.  That convenience comes with the
// cost of those checks...so it is more optimal to return an enumeration code
// saying the value is already in f->out.  And entire cells must be moved
// into the out position instead of just setting headers, for unit types.
//
// It's not terribly significant, but `return R_VOID;` in a native is slightly
// faster than `return VOID_CELL;`, and `Move_Value(D_OUT, t); return D_OUT;`
// will also be slightly faster than `return t;`
//
// NOTE: Initially the letters were chosen to be meaningful ('F' for false,
// '*' for thrown since 'T' was for true, etc.).  But being discontiguous
// meant less optimization opportunity, for slight effect:
//
// http://stackoverflow.com/questions/17061967/c-switch-and-jump-tables
//
// So they are now boring integer byte values counting up from 0.  Given a
// name that includes the numbers so switch statements can make sure they
// get all of them in order and there aren't gaps.
//
// NOTE: A REBVAL *is a superset of const REBVAL*, but defined as one just for
// convenience, to avoid worrying about casting.

#define R_00_FALSE 0x00
#define R_FALSE cast(const REBVAL*, &PG_R_FALSE)
#define R_01_TRUE 0x01
#define R_TRUE cast(const REBVAL*, &PG_R_TRUE)
#define R_02_VOID 0x02
#define R_VOID cast(const REBVAL*, &PG_R_VOID)
#define R_03_BLANK 0x03
#define R_BLANK cast(const REBVAL*, &PG_R_BLANK)
#define R_04_BAR 0x04
#define R_BAR cast(const REBVAL*, &PG_R_BAR)

// If Do_Core gets back an R_REDO from a dispatcher, it will re-execute the
// f->phase in the frame.  This function may be changed by the dispatcher from
// what was originally called.
//
// It can be asked that the types be checked again, or not (note it is not
// safe to let arbitrary user code change values in a frame from expected
// types, and then let those reach an underlying native who thought the types
// had been checked.)
//
#define R_05_REDO_CHECKED 0x05
#define R_REDO_CHECKED cast(const REBVAL*, &PG_R_REDO_UNCHECKED)
#define R_06_REDO_UNCHECKED 0x06
#define R_REDO_UNCHECKED cast(const REBVAL*, &PG_R_REDO_UNCHECKED)

// EVAL is special because it stays at the frame level it is already running,
// but re-evaluates.  In order to do this, it must protect its argument during
// that evaluation, so it writes into the frame's "eval cell".
//
#define R_07_REEVALUATE_CELL 0x07
#define R_REEVALUATE_CELL cast(const REBVAL*, &PG_R_REEVALUATE_CELL)
#define R_08_REEVALUATE_CELL_ONLY 0x08
#define R_REEVALUATE_CELL_ONLY cast(const REBVAL*, &PG_R_REEVALUATE_CELL_ONLY)

// See ACTION_FLAG_INVISIBLE...this is what any function with that flag needs
// to return.
//
// It is also used by path dispatch when it has taken performing a SET-PATH!
// into its own hands, but doesn't want to bother saying to move the value
// into the output slot...instead leaving that to the evaluator (as a
// SET-PATH! should always evaluate to what was just set)
//
#define R_09_INVISIBLE 0x09
#define R_INVISIBLE cast(const REBVAL*, &PG_R_INVISIBLE)

// Path dispatch used to have a return value PE_SET_IF_END which meant that
// the dispatcher itself should realize whether it was doing a path get or
// set, and if it were doing a set then to write the value to set into the
// target cell.  That means it had to keep track of a pointer to a cell vs.
// putting the bits of the cell into the output.  This is now done with a
// special REB_0_REFERENCE type which holds in its payload a RELVAL and a
// specifier, which is enough to be able to do either a read or a write,
// depending on the need.
//
// !!! See notes in %c-path.c of why the R3-Alpha path dispatch is hairier
// than that.  It hasn't been addressed much in Ren-C yet, but needs a more
// generalized design.
//
#define R_0A_REFERENCE 0x0A
#define R_REFERENCE cast(const REBVAL*, &PG_R_REFERENCE)

// This is used in path dispatch, signifying that a SET-PATH! assignment
// resulted in the updating of an immediate expression in pvs->out, meaning
// it will have to be copied back into whatever reference cell it had been in.
//
#define R_0B_IMMEDIATE 0x0B
#define R_IMMEDIATE cast(const REBVAL*, &PG_R_IMMEDIATE)

// This is a signal that isn't accepted as a return value from a native, so it
// can be used by common routines that return REBVAL *values and need an
// "escape" code.  (nullptr wouldn't allow the FIRST_BYTE() switch check)
//
#define R_0C_UNHANDLED 0x0C
#define R_UNHANDLED cast(const REBVAL*, &PG_R_UNHANDLED)

// Used as a signal from Do_Vararg_Op_May_Throw
//
#define R_0D_END 0x0D
#define R_END cast(const REBVAL*, &PG_R_END)

#define R_0E_THROWN 0x0E
#define R_THROWN cast(const REBVAL*, &PG_R_THROWN)
