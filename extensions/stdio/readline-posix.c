//
//  File: %host-readline.c
//  Summary: "Simple readline() line input handler"
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
// Processes special keys for input line editing and recall.
//
// Avoids use of complex OS libraries and GNU readline() but hardcodes some
// parts only for the common standard.
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h> //for read and write
#include <errno.h>

#ifndef NO_TTY_ATTRIBUTES
    #include <termios.h>
#endif


//=//// REBOL INCLUDES + HELPERS //////////////////////////////////////////=//

#include "sys-core.h"

#define xrebWord(cstr) \
    rebValue("'" cstr, rebEND)  // C string literal should merge apostrophe


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

#define READ_BUF_LEN 64   // chars per read()
#define MAX_HISTORY  300   // number of lines stored


#define WRITE_CHAR(s) \
    do { \
        if (write(1, s, 1) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

#define WRITE_UTF8(s,n) \
    do { \
        if (write(1, s, n) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

inline static void WRITE_STR(const char *s) {
    do {
        if (write(1, s, strlen(s)) == -1) {
            /* Error here, or better to "just try to keep going"? */
        }
    } while (0);
}


typedef struct term_data {
    REBVAL *buffer;  // a TEXT! used as a buffer
    int pos;  // cursor position within the line
    int hist;  // history position within the line history buffer

    unsigned char buf[READ_BUF_LEN];  // '\0' terminated, needs -1 on read()
    const unsigned char *cp;
} STD_TERM;

inline static int Term_End(STD_TERM *term)
  { return rebUnboxInteger("length of", term->buffer, rebEND); }

inline static int Term_Remain(STD_TERM *term) {
    return rebUnboxInteger(
        "length of skip", term->buffer, rebI(term->pos),
    rebEND);
}


//=//// GLOBALS ///////////////////////////////////////////////////////////=//

static bool Term_Initialized = false;  // Terminal init was successful
static REBVAL *Line_History;  // Prior input lines (BLOCK!)
#define Line_Count \
    rebUnboxInteger("length of", Line_History, rebEND)

#ifndef NO_TTY_ATTRIBUTES
    static struct termios Term_Attrs;  // Initial settings, restored on exit
#endif


extern STD_TERM *Init_Terminal(void);

//
//  Init_Terminal: C
//
// If possible, change the terminal to "raw" mode (where characters are
// received one at a time, as opposed to "cooked" mode where a whole line is
// read at once.)
//
STD_TERM *Init_Terminal(void)
{
  #ifndef NO_TTY_ATTRIBUTES
    //
    // Good reference on termios:
    //
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios/
    // https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios-termios3-and-stty/
    // https://blog.nelhage.com/2010/01/a-brief-introduction-to-termios-signaling-and-job-control/
    //
    struct termios attrs;

    if (Term_Initialized || tcgetattr(0, &Term_Attrs)) return NULL;

    attrs = Term_Attrs;

    // Local modes.
    //
    attrs.c_lflag &= ~(ECHO | ICANON);  // raw input

    // Input modes.  Note later Linuxes have a IUTF8 flag that POSIX doesn't,
    // but it seems to only affect the "cooked" mode (as opposed to "raw").
    //
    attrs.c_iflag &= ~(ICRNL | INLCR);  // leave CR and LF as-is

    // Output modes.  If you don't add ONLCR then a single `\n` will just go
    // to the next line and not put the cursor at the start of that line.
    // So ONLCR is needed for the typical unix expectation `\n` does both.
    //
    attrs.c_oflag |= ONLCR;  // On (O)utput, map (N)ew(L)ine to (CR) LF

    // Special modes.
    //
    attrs.c_cc[VMIN] = 1;   // min num of bytes for READ to return
    attrs.c_cc[VTIME] = 0;  // how long to wait for input

    tcsetattr(0, TCSADRAIN, &attrs);
  #endif

    // !!! Ultimately, we want to be able to recover line history from a
    // file across sessions.  It makes more sense for the logic doing that
    // to be doing it in Rebol.  For starters, we just make it fresh.
    //
    Line_History = rebValue("[{}]", rebEND);  // current line is empty string
    rebUnmanage(Line_History);  // allow Line_History to live indefinitely

    STD_TERM *term = cast(STD_TERM*, malloc(sizeof(STD_TERM)));
    term->buffer = rebValue("{}", rebEND);
    rebUnmanage(term->buffer);

    term->buf[0] = '\0';  // start read() byte buffer out at empty
    term->cp = term->buf;

    Term_Initialized = true;

    return term;
}


extern void Quit_Terminal(STD_TERM *term);

//
//  Quit_Terminal: C
//
// Restore the terminal modes original entry settings,
// in preparation for exit from program.
//
void Quit_Terminal(STD_TERM *term)
{
    if (Term_Initialized) {
      #ifndef NO_TTY_ATTRIBUTES
        tcsetattr(0, TCSADRAIN, &Term_Attrs);
      #endif

        rebRelease(term->buffer);
        free(term);

        rebRelease(Line_History);
        Line_History = nullptr;
    }

    Term_Initialized = false;
}


//
//  Read_Bytes_Interrupted: C
//
// Read the next "chunk" of data into the terminal buffer.  If it gets
// interrupted then return true, else false.
//
// Note that The read of bytes might end up getting only part of an encoded
// UTF-8 character.  But it's known how many bytes are expected from the
// leading byte.
//
// Escape sequences could also *theoretically* be split, and they have no
// standard for telling how long the sequence could be.  (ESC '\0') could be a
// plain escape key--or it could be an unfinished read of a longer sequence.
// We assume this won't happen, because the escape sequences being entered
// usually happen one at a time (cursor up, cursor down).  Unlike text, these
// are not *likely* to be pasted in a batch that could overflow READ_BUF_LEN
// and be split up.
//
static bool Read_Bytes_Interrupted(STD_TERM *t)
{
    assert(*t->cp == '\0');  // Don't read more bytes if buffer not exhausted

    int len = read(0, t->buf, READ_BUF_LEN - 1);  // save space for '\0'
    if (len < 0) {
        if (errno == EINTR)
            return true;  // Ctrl-C or similar, see sigaction()/SIGINT

        WRITE_STR("\nI/O terminated\n");
        Quit_Terminal(t);  // something went wrong
        exit(100);
    }

    t->buf[len] = '\0';
    t->cp = t->buf;

    return false;  // not interrupted (note we could return `len` if needed)
}


//
//  Write_Char: C
//
// Write out repeated number of chars.
//
static void Write_Char(unsigned char c, int n)
{
    unsigned char buf[4];

    buf[0] = c;
    for (; n > 0; n--)
        WRITE_CHAR(buf);
}


//
//  Store_Line: C
//
// Stores a copy of the current buffer in the history list.
//
static void Store_Line(STD_TERM *term)
{
    // If max history, drop oldest line (but not first empty line)
    //
    if (Line_Count >= MAX_HISTORY)
        rebElide("remove next", Line_History, rebEND);

    rebElide("append", Line_History, "copy", term->buffer, rebEND);
}


//
//  Recall_Line: C
//
// Set the current buffer to the contents of the history
// list at its current position. Clip at the ends.
//
static void Recall_Line(STD_TERM *term)
{
    if (term->hist < 0)
        term->hist = 0;

    if (term->hist == 0)
        Write_Char(BEL, 1);  // try an audible alert if no previous history

    // Rather than rebRelease() the buffer, we clear it and append to it if
    // there is content to draw in.  This saves the GC some effort.
    //
    rebElide("clear", term->buffer, rebEND);

    if (term->hist >= Line_Count) {  // no "next" line, so clear buffer
        term->hist = Line_Count;
        term->pos = 0;
    }
    else {
        rebElide(
            "append", term->buffer,  // see above GC note on CLEAR + APPEND
                "pick", Line_History, rebI(term->hist + 1),  // 1-based
        rebEND);
        term->pos = Term_End(term);
    }
}


//
//  Clear_Line: C
//
// Clear all the chars from the current position to the end.
// Reset cursor to current position.
//
static void Clear_Line(STD_TERM *term)
{
    int num_codepoints_to_end = Term_Remain(term);

    Write_Char(' ', num_codepoints_to_end);  // wipe to end of line...
    Write_Char(BS, num_codepoints_to_end);  // ...then return to position
}


//
//  Home_Line: C
//
// Reset cursor to home position.
//
static void Home_Line(STD_TERM *term)
{
    while (term->pos > 0) {
        --term->pos;
        Write_Char(BS, 1);
    }
}


//
//  End_Line: C
//
// Move cursor to end position.
//
static void End_Line(STD_TERM *term)
{
    int num_codepoints = Term_Remain(term);

    if (num_codepoints != 0) {
        size_t size;
        unsigned char *utf8 = rebBytes(&size,
            "skip", term->buffer, rebI(term->pos),
        rebEND);

        WRITE_UTF8(utf8, size);
        term->pos += num_codepoints;

        rebFree(utf8);
    }
}


//
//  Show_Line: C
//
// Refresh a line from the current position to the end.
// Extra blanks can be specified to erase chars off end.
// If blanks is negative, stay at end of line.
// Reset the cursor back to current position.
//
static void Show_Line(STD_TERM *term, int blanks)
{
    // Clip bounds
    //
    int end = Term_End(term);
    if (term->pos < 0)
        term->pos = 0;
    else if (term->pos > end)
        term->pos = end;

    if (blanks >= 0) {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            "skip", term->buffer, rebI(term->pos),
        rebEND);

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);
    }
    else {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            term->buffer,
        rebEND);

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);

        blanks = -blanks;
    }

    Write_Char(' ', blanks);
    Write_Char(BS,  blanks);  // return to original position or end

    // We want to write as many backspace characters as there are *codepoints*
    // in the buffer to end of line.
    //
    Write_Char(BS, Term_Remain(term));
}


//
//  Delete_Char: C
//
// Delete a char at the current position. Adjust end position.
// Redisplay the line. Blank out extra char at end.
//
static void Delete_Char(STD_TERM *term, bool back)
{
    int end = Term_End(term);

    if (term->pos == end and not back)
        return;  // Ctrl-D (forward-delete) at end of line

    if (term->pos == 0 and back)
        return;  // backspace at beginning of line

    if (back)
        --term->pos;

    if (term->pos >= 0 and end > 0) {
        rebElide(
            "remove skip", term->buffer, rebI(term->pos),
        rebEND);

        if (back)
            Write_Char(BS, 1);

        Show_Line(term, 1);
    }
    else
        term->pos = 0;
}


//
//  Move_Cursor: C
//
// Move cursor right or left by one char.
//
static void Move_Cursor(STD_TERM *term, int count)
{
    if (count < 0) {
        //
        // "backspace" in TERMIOS lets you move the cursor left without
        //  knowing what character is there and without overwriting it.
        //
        if (term->pos > 0) {
            --term->pos;
            Write_Char(BS, 1);
        }
    }
    else {
        // Moving right without affecting a character requires writing the
        // character you know to be already there (via the buffer).
        //
        int end = Term_End(term);
        if (term->pos < end) {
            size_t encoded_size;
            unsigned char *encoded_char = rebBytes(&encoded_size,
                "to binary! pick", term->buffer, rebI(term->pos + 1),
            rebEND);
            WRITE_UTF8(encoded_char, encoded_size);
            rebFree(encoded_char);

            term->pos += 1;
        }
    }
}


// When an unrecognized key is hit, people may want to know that at least the
// keypress was received.  Or not.  For now just give a message in the debug
// build.
//
// !!! In the future, this might do something more interesting to get the
// BINARY! information for the key sequence back up out of the terminal, so
// that people could see what the key registered as on their machine and
// configure their console to respond to it.
//
// !!! Given the way the code works, escape sequences should be able to span
// buffer reads, and the current method of passing in subtracted codepoint
// addresses wouldn't work since `cp` can change on spanned reads.  This
// should probably be addressed rigorously if one wanted to actually do
// something with `delta`, but code is preserved as it was for annotation.
//
inline static REBVAL *Unrecognized_Key_Sequence(STD_TERM *t, int delta)
{
    assert(delta <= 0);
    UNUSED(delta);

    // We don't really know how long an incomprehensible escape sequence is.
    // For now, just drop all the data, pending better heuristics or ideas.
    //
    t->buf[0] = '\0';
    t->cp = t->buf;

    return rebText("[KEY?]");
}


//
//  Try_Get_One_Console_Event: C
//
// This attempts to get one unit of "event" from the console.  It does not
// use the Rebol EVENT! datatype at this time.  Instead it returns:
//
//    CHAR! => a printable character
//    WORD! => keystroke or control code
//    TEXT! => depiction of an unknown key combination
//    VOID! => interrupted by HALT or Ctrl-C
//
// It does not do any printing or handling while fetching the event.
//
// Note Ctrl-C comes from the SIGINT signal and not from the physical detection
// of the key combination "Ctrl + C", which this routine should not receive
// due to deferring to the default UNIX behavior for that (otherwise, scripts
// could not be cancelled unless they were waiting at an input prompt).
//
// !!! The idea is that if there is no event available, this routine will
// return a nullptr.  That would allow some way of exiting the read() to
// do another operation (process network requests for a real-time chat, etc.)
// This is at the concept stage at the moment.
//
REBVAL *Try_Get_One_Console_Event(STD_TERM *t)
{
    REBVAL *key = nullptr;

    // See notes on why Read_Bytes_Interrupted() can wind up splitting UTF-8
    // encodings (which can happen with pastes of text).
    //
    // Also see notes there on why escape sequences are anticipated to come
    // in one at a time, and there's no good way of handling unrecognized
    // sequences.
    //
    if (*t->cp == '\0') {  // no residual bytes from a previous read pending
        if (Read_Bytes_Interrupted(t))
            return rebVoid();  // signal a HALT

        assert(*t->cp != '\0');
    }

    if (
        (*t->cp >= 32 and *t->cp < 127)  // 32 is space, 127 is DEL(ete)
        or *t->cp > 127  // high-bit set UTF-8 start byte
    ){
    //=//// ASCII printable character or UTF-8 ////////////////////////////=//
        //
        // https://en.wikipedia.org/wiki/ASCII
        // https://en.wikipedia.org/wiki/UTF-8
        //
        // A UTF-8 character may span multiple bytes...and if the buffer end
        // was reached on a partial read() of a UTF-8 character, we may need
        // to do more reading to get the missing bytes here.

        int encoded_size = 1 + trailingBytesForUTF8[*t->cp];
        assert(encoded_size <= 4);

        // `cp` can jump back to the beginning of the buffer on each read.
        // So build up an encoded UTF-8 character as continuous bytes so it
        // can be inserted into a Rebol string atomically.
        //
        char encoded[4];
        int i;
        for (i = 0; i < encoded_size; ++i) {
            if (*t->cp == '\0') {
                //
                // Premature end, the UTF-8 data must have gotten split on
                // a buffer boundary.  Refill the buffer with another read,
                // where the remaining UTF-8 characters *should* be found.
                //
                if (Read_Bytes_Interrupted(t))
                    return nullptr;  // signal a HALT
            }
            assert(*t->cp != '\0');
            encoded[i] = *t->cp;
            ++t->cp;
        }

        key = rebValue(
            "to char!", rebR(rebSizedBinary(encoded, encoded_size)),
        rebEND);
    }
    else if (*t->cp == ESC and t->cp[1] == '\0') {

    //=//// Plain Escape //////////////////////////////////////////////////=//

        key = xrebWord("escape");
    }
    else if (*t->cp == ESC and t->cp[1] == '[') {

    //=//// CSI Escape Sequences, VT100/VT220 Escape Sequences, etc. //////=//
        //
        // https://en.wikipedia.org/wiki/ANSI_escape_code#CSI_sequences
        // http://ascii-table.com/ansi-escape-sequences-vt-100.php
        // http://aperiodic.net/phil/archives/Geekery/term-function-keys.html
        //
        // While these are similar in beginning with ESC and '[', the
        // actual codes vary.  HOME in CSI would be (ESC '[' '1' '~').
        // But to HOME in VT100, it can be as simple as (ESC '[' 'H'),
        // although there can be numbers between the '[' and 'H'.
        //
        // There's not much in the way of "rules" governing the format of
        // sequences, though official CSI codes always fit this pattern
        // with the following sequence:
        //
        //    the ESC then the '[' ("the CSI")
        //    one of `0-9:;<=>?` ("parameter byte")
        //    any number of `!"# $%&'()*+,-./` ("intermediate bytes")
        //    one of `@A-Z[\]^_`a-z{|}~` ("final byte")
        //
        // But some codes might look like CSI codes while not actually
        // fitting that rule.  e.g. the F8 function key on my machine
        // generates (ESC '[' '1' '9' '~'), which is a VT220 code
        // conflicting with the CSI interpretation of HOME above.
        //
        // Note: This kind of conflict confuses "linenoise", leading F8 to
        // jump to the beginning of line and display a tilde:
        //
        // https://github.com/antirez/linenoise

        t->cp += 2;  // skip ESC and '['

        switch (*t->cp) {
          case 'A':  // up arrow (VT100)
            key = xrebWord("up");
            break;

          case 'B':  // down arrow (VT100)
            key = xrebWord("down");
            break;

          case 'D':  // left arrow (VT100)
            key = xrebWord("left");
            break;

          case 'C':  // right arrow (VT100)
            key = xrebWord("right");
            break;

          case '1':  // home (CSI) or higher function keys (VT220)
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            key = xrebWord("home");
            ++t->cp;  // remove 1, the ~ is consumed after the switch
            break;

          case '4': // end (CSI)
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            key = xrebWord("end");
            ++t->cp;  // remove 4, the ~ is consumed after the switch
            break;

          case '3':  // delete (CSI)
            if (t->cp[1] != '~')
                return Unrecognized_Key_Sequence(t, -2);

            key = xrebWord("delete");
            ++t->cp;  // remove 3, the ~ is consumed after the switch
            break;

          case 'H':  // home (VT100)
            key = xrebWord("home");
            break;

          case 'F':  // end !!! (in what standard?)
            key = xrebWord("end");
            break;

          case 'J':  // erase to end of screen (VT100)
            key = xrebWord("clear");
            break;

          default:
            return Unrecognized_Key_Sequence(t, -2);
        }

        ++t->cp;
    }
    else if (*t->cp == ESC) {

    //=//// non-CSI Escape Sequences //////////////////////////////////////=//
        //
        // http://ascii-table.com/ansi-escape-sequences-vt-100.php

        ++t->cp;

        switch (*t->cp) {
          case 'H':   // !!! "home" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps(
                "FAIL {ESC H: please report your system info}",
            rebEND);
          #endif
            key = xrebWord("home");
            break;

          case 'F':  // !!! "end" (in what standard??)
          #if !defined(NDEBUG)
            rebJumps(
                "FAIL {ESC F: please report your system info}",
            rebEND);
          #endif
            key = xrebWord("end");
            break;

          case '\0':
            assert(false);  // plain escape handled earlier for clarity
            key = xrebWord("escape");
            break;

          default:
            return Unrecognized_Key_Sequence(t, -2);
        }

        ++t->cp;
    }
    else {

    //=//// C0 Control Codes and Bash-inspired Shortcuts //////////////////=//
        //
        // https://en.wikipedia.org/wiki/C0_and_C1_control_codes
        // https://ss64.com/bash/syntax-keyboard.html
    
        if (*t->cp == 3)  {  // CTRL-C, Interrupt (ANSI, <signal.h> is C89)
            //
            // It's theoretically possible to clear the termios `c_lflag` ISIG
            // in order to receive literal Ctrl-C, but we don't want to get
            // involved at that level.  Using sigaction() on SIGINT and
            // causing EINTR is how we would like to be triggering HALT.
            //
            rebJumps(
                "FAIL {Unexpected literal Ctrl-C in console}",
            rebEND);
        }
        else switch (*t->cp) {
          case DEL:  // delete (C0)
            //
            // From Wikipedia:
            // "On modern systems, terminal emulators typically turn keys
            // marked "Delete" or "Del" into an escape sequence such as
            // ^[[3~. Terminal emulators may produce DEL when backspace
            // is pressed."
            //
            // We assume "modern" interpretation of DEL as backspace synonym.
            //
            goto backspace;

          case BS:  // backspace (C0)
          backspace:
            key = xrebWord("backspace");
            break;

          case CR:  // carriage return (C0)
            if (t->cp[1] == LF)
                ++t->cp;  // disregard the CR character, else treat as LF
            goto line_feed;

          line_feed:;
          case LF:  // line feed (C0)
            key = rebChar('\n');  // default case would do it, but be clear
            break;

          default:
            if (*t->cp >= 1 and *t->cp <= 26) {  // Ctrl-A, Ctrl-B, etc.
                key = rebValue(
                    "as word! unspaced [",
                        "{ctrl-}", rebR(rebChar(*t->cp - 1 + 'a')),
                    "]",
                rebEND);
            }
            else
                return Unrecognized_Key_Sequence(t, 0);
        }
        ++t->cp;
    }

    assert(key != nullptr);
    return key;
}


extern REBVAL *Read_Line(STD_TERM *term);


//
//  Term_Insert: C
//
// Inserts a Rebol value (TEXT!, CHAR!, etc.) at the current cursor position.
//
void Term_Insert(STD_TERM *t, const REBVAL *v) {
    REBVAL *text = rebValue("to text!", v, rebEND);

    rebElide(
        "insert skip", t->buffer, rebI(t->pos), text,
    rebEND);

    int len = rebUnboxInteger("length of", text, rebEND);

    size_t encoded_size;
    unsigned char *encoded = rebBytes(&encoded_size,
        text,
    rebEND);
    WRITE_UTF8(encoded, encoded_size);
    rebFree(encoded);

    rebRelease(text);

    t->pos += len;
    Show_Line(t, 0);
}


//
//  Read_Line: C
//
// Read a line (as a sequence of bytes) from the terminal.  Handles line
// editing and line history recall.
//
// If HALT is encountered (e.g. a Ctrl-C), this routine will return VOID!
// If ESC is pressed, this will return a BLANK!.
// Otherwise it will return a TEXT! of the read-in string.
//
// !!! Read_Line is a transitional step as a C version of what should move to
// be usermode Rebol, making decisions about communication with the terminal
// on a keystroke-by-keystroke basis.
//
REBVAL *Read_Line(STD_TERM *t)
{
    t->pos = 0;
    t->hist = Line_Count;

    rebElide("clear", t->buffer, rebEND);

    REBVAL *line = nullptr;
    while (line == nullptr) {
        REBVAL *e = Try_Get_One_Console_Event(t);  // (not an actual EVENT!)

        if (e == nullptr) {
            rebJumps(
                "fail {nullptr interruption of terminal not done yet}",
            rebEND);
        }
        else if (rebDid("void?", rebQ1(e), rebEND)) {
            line = rebVoid();
        }
        else if (rebDidQ(e, "= newline", rebEND)) {
            //
            // !!! This saves a line in the "history", but it's not clear
            // exactly long term what level this history should cut into
            // the system.
            //
            Store_Line(t);

            // We could return the term buffer directly and allocate a new
            // buffer.  But returning a copy lets the return result be a new
            // allocation at the exact size of the final input (e.g. paging
            // through the history and getting longer entries transiently
            // won't affect the extra capacity of what's ultimately returned.)
            //
            line = rebValue("copy", t->buffer, rebEND);
        }
        else if (rebDidQ("char?", e, rebEND)) {  // printable character
            Term_Insert(t, e);
        }
        else if (rebDidQ("word?", e, rebEND)) {  // recognized "virtual key"
            uint32_t ch = rebUnboxChar(
                "to char! switch", rebQ1(e), "[",
                    "'escape ['E]",

                    "'up ['U]",
                    "'down ['D]",
                    "'ctrl-b",  // Backward One Character (bash)
                        "'left ['L]",
                    "'ctrl-f",  // Forward One Character (bash)
                        "'right ['R]",

                    "'backspace ['b]",
                    "'ctrl-d",  // Delete Character Under Cursor (bash)
                        "'delete ['d]",

                    "'ctrl-a",  // Beginning of Line (bash)
                        "'home ['h]",
                    "'ctrl-e",  // CTRL-E, end of Line (bash)
                        "'end ['e]",

                    "'clear ['c]",

                    "default [0]",
                "]",
            rebEND);

            switch (ch) {
              case 'E':  // ESCAPE
                line = rebBlank();
                break;

              case 'U':  // UP
                t->hist -= 2;  // overcompensate, then down arrow subtracts 1
                goto down_arrow;  // ...has otherwise same updating code...

              down_arrow:;
              case 'D': {  // DOWN
                int old_end = Term_End(t);

                ++t->hist;

                Home_Line(t);
                Recall_Line(t);

                int new_end = Term_End(t);

                int len;
                if (old_end <= new_end)
                    len = 0;
                else
                    len = new_end - old_end;

                Show_Line(t, len - 1);  // len < 0 (stay at end)
                break; }

              case 'L':  // LEFT
                Move_Cursor(t, -1);
                break;

              case 'R':  // RIGHT
                Move_Cursor(t, 1);
                break;

              case 'b':  // backspace
                Delete_Char(t, true);
                break;

              case 'd':  // delete
                Delete_Char(t, false);
                break;

              case 'h':  // home
                Home_Line(t);
                break;

              case 'e':  // end
                End_Line(t);
                break;

              case 'c':  // clear
                Clear_Line(t);
                break;

              default:
                rebJumps(
                    "fail {Invalid key press returned from console}",
                rebEND);
            }
        }
        else if (rebDidQ("text?", e, rebEND)) {  // unrecognized key
            //
            // When an unrecognized key is hit, people may want to know that
            // at least the keypress was received.  Or not.  For now, output
            // a key message to say "we don't know what you hit".
            //
            // !!! In the future, this might do something more interesting to
            // get the BINARY! information for the key sequence back up out of
            // the terminal, so that people could see what the key registered
            // as on their machine and configure the console to respond to it.
            //
            Term_Insert(t, e);
        }

        rebRelease(e);
    }

    // ASK has a display invariant that a newline is visually expected as part
    // of what the user contributed.  We print one out whether we got a whole
    // line or not (e.g. ESCAPE or HALT) to keep the visual flow.
    //
    WRITE_STR("\n");

    return line;
}
