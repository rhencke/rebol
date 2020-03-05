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

#include "sys-core.h"


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

#define RESIDUE_LEN 4096  // length of residue
#define READ_BUF_LEN 64  // chars per read()
#define MAX_HISTORY  300  // number of lines stored


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

    unsigned char *residue;
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

    term->residue = cast(unsigned char*, malloc(sizeof(char) * RESIDUE_LEN));
    term->residue[0] = 0;

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
        free(term->residue);
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
// leading byte.  Input_Char() can handle it by requesting the missing ones.
//
// Escape sequences could also *theoretically* be split, and they have no
// standard for telling how long the sequence could be.  (ESC '\0') could be a
// plain escape key--or it could be an unfinished read of a longer sequence.
// We assume this won't happen, because the escape sequences being entered
// usually happen one at a time (cursor up, cursor down).  Unlike text, these
// are not *likely* to be pasted in a batch that could overflow READ_BUF_LEN
// and be split up.
//
static bool Read_Bytes_Interrupted(STD_TERM *term, unsigned char *buf, int len)
{
    // If we have leftovers:
    //
    if (term->residue[0] != '\0') {
        int end = LEN_BYTES(term->residue);
        if (end < len)
            len = end;
        strncpy(s_cast(buf), s_cast(term->residue), len); // terminated below
        memmove(term->residue, term->residue + len, end - len); // remove
        term->residue[end - len] = '\0';
    }
    else {
        if ((len = read(0, buf, len)) < 0) {
            if (errno == EINTR)
                return true; // Ctrl-C or similar, see sigaction()/SIGINT

            WRITE_STR("\nI/O terminated\n");
            Quit_Terminal(term); // something went wrong
            exit(100);
        }
    }

    buf[len] = '\0';

    return false; // not interrupted, note we could return `len` if needed
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
//  Insert_Char_Null_If_Interrupted: C
//
// * Insert a char at the current position.
// * Adjust end position.
// * Redisplay the line.
//
static const unsigned char *Insert_Char_Null_If_Interrupted(
    STD_TERM *term,
    unsigned char *buf,
    int limit,
    const unsigned char *cp  // likely points into `buf` (possibly at end)
){
    int encoded_len = 1 + trailingBytesForUTF8[*cp];
    assert(encoded_len <= 4);

    // Build up an encoded UTF-8 character as continuous bytes so it can be
    // inserted into a Rebol string.  The component bytes may span the passed
    // in cp, and additional buffer reads.
    //
    char encoded[4];
    int i;
    for (i = 0; i < encoded_len; ++i) {
        if (*cp == '\0') {
            //
            // Premature end, the UTF-8 data must have gotten split on
            // a buffer boundary.  Refill the buffer with another read,
            // where the remaining UTF-8 characters *should* be found.
            //
            if (Read_Bytes_Interrupted(term, buf, limit - 1))
                return nullptr;  // signal interruption

            cp = buf;
        }
        assert(*cp != '\0');
        encoded[i] = *cp;
        ++cp;
    }

    rebElide(
        "insert skip", term->buffer, rebI(term->pos),
            rebR(rebSizedText(encoded, encoded_len)),
    rebEND);
    WRITE_UTF8(encoded, encoded_len);
    ++term->pos;

    Show_Line(term, 0);

    return cp;
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
inline static void Unrecognized_Key_Sequence(const unsigned char* cp)
{
    UNUSED(cp);

  #if !defined(NDEBUG)
    WRITE_STR("[KEY?]");
  #endif
}

extern REBVAL *Read_Line(STD_TERM *term);

//
//  Read_Line: C
//
// Read a line (as a sequence of bytes) from the terminal.  Handles line
// editing and line history recall.
//
// If HALT is encountered (e.g. a Ctrl-C), this routine will return nullptr.
// If ESC is pressed, this will return a BLANK!.
// Otherwise it will return a TEXT! of the read-in string.
//
REBVAL *Read_Line(STD_TERM *term)
{
    term->pos = 0;
    term->hist = Line_Count;

    rebElide("clear", term->buffer, rebEND);

  restart:;
    //
    // See notes on why Read_Bytes_Interrupted() can wind up splitting UTF-8
    // encodings (which can happen with pastes of text), and this is handled
    // by Insert_Char_Null_If_Interrupted().
    //
    // Also see notes there on why escape sequences are anticipated to come
    // in one at a time.  Hence unrecognized escape sequences jump up here to
    // `restart`.  Thus, hitting an unknown escape sequence and a character
    // very fast after it may discard that character.
    //
    unsigned char buf[READ_BUF_LEN]; // '\0' terminated, hence `- 1` below
    const unsigned char *cp = buf;

    if (Read_Bytes_Interrupted(term, buf, READ_BUF_LEN - 1))
        goto return_halt;

    while (*cp != '\0') {
        if (
            (*cp >= 32 and *cp < 127)  // 32 is space, 127 is DEL(ete)
            or *cp > 127  // high-bit set UTF-8 start byte
        ){
            // ASCII printable character or UTF-8
            //
            // https://en.wikipedia.org/wiki/ASCII
            // https://en.wikipedia.org/wiki/UTF-8
            //
            // Inserting a character may consume multiple bytes...and if the
            // buffer end was reached on a partial input of a UTF-8 character,
            // it may need to do its own read in order to get the missing
            // bytes and reset the buffer pointer.  So it can adjust cp even
            // backwards, if such a read is done.
            //
            cp = Insert_Char_Null_If_Interrupted(term, buf, READ_BUF_LEN, cp);
            if (cp == nullptr)
                goto return_halt;

            continue;
        }

        if (*cp == ESC and cp[1] == '\0') {
            //
            // Plain Escape - Cancel Current Input (...not Halt Script)
            //
            // There are two distinct ways we want INPUT to be canceled.  One
            // is in a way that a script can detect, and continue running:
            //
            //    print "Enter filename (ESC to return to main menu):"
            //    if not filename: input [
            //       return 'go-to-main-menu
            //    ]
            //
            // The other kind of aborting would stop the script from running
            // further entirely...and either return to the REPL or exit the
            // Rebol process.  By near-universal convention in programming,
            // this is Ctrl-C (SIGINT - Signal Interrupt).  ESC seems like
            // a reasonable choice for the other.
            //
            // The way the notice of abort is wound up to the INPUT command
            // through the (deprecated) R3-Alpha "OS Host" is by a convention
            // that aborted lines will be an escape char and a terminator.
            //
            // The convention here seems to be that there's a terminator after
            // the length returned.
            //
            goto return_blank;

            // !!! See notes in the Windows Terminal usage of ReadConsole()
            // about how ESC cannot be overridden when using ENABLE_LINE_INPUT
            // to do anything other than clear the line.  Ctrl-D is used
            // there instead.
        }

        if (*cp == ESC and cp[1] == '[') {
            //
            // CSI Escape Sequences, VT100/VT220 Escape Sequences, etc:
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

            cp += 2; // skip ESC and '['

            switch (*cp) {
              case 'A': // up arrow (VT100)
                term->hist -= 2;
                goto down_arrow;

              down_arrow:;
              case 'B': { // down arrow (VT100)
                int old_end = Term_End(term);

                ++term->hist;

                Home_Line(term);
                Recall_Line(term);

                int new_end = Term_End(term);

                int len;
                if (old_end <= new_end)
                    len = 0;
                else
                    len = new_end - old_end;

                Show_Line(term, len - 1); // len < 0 (stay at end)
                break; }

              case 'D': // left arrow (VT100)
                Move_Cursor(term, -1);
                break;

              case 'C': // right arrow (VT100)
                Move_Cursor(term, 1);
                break;

              case '1': // home (CSI) or higher function keys (VT220)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                Home_Line(term);
                ++cp; // remove 1, the ~ is consumed after the switch
                break;

              case '4': // end (CSI)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                End_Line(term);
                ++cp; // remove 4, the ~ is consumed after the switch
                break;

              case '3': // delete (CSI)
                if (cp[1] != '~') {
                    Unrecognized_Key_Sequence(cp - 2);
                    goto restart;
                }
                Delete_Char(term, false);
                ++cp; // remove 3, the ~ is consumed after the switch
                break;

              case 'H': // home (VT100)
                Home_Line(term);
                break;

              case 'F': // end !!! (in what standard?)
                End_Line(term);
                break;

              case 'J': // erase to end of screen (VT100)
                Clear_Line(term);
                break;

              default:
                Unrecognized_Key_Sequence(cp - 2);
                goto restart;
            }

            ++cp;
            continue;
        }

        if (*cp == ESC) {
            //
            // non-CSI Escape Sequences
            //
            // http://ascii-table.com/ansi-escape-sequences-vt-100.php

            ++cp;

            switch (*cp) {
              case 'H':   // !!! "home" (in what standard??)
              #if !defined(NDEBUG)
                rebJumps(
                    "FAIL {ESC H: please report your system info}",
                    rebEND
                );
              #endif
                Home_Line(term);
                break;

              case 'F':   // !!! "end" (in what standard??)
              #if !defined(NDEBUG)
                rebJumps(
                    "FAIL {ESC F: please report your system info}",
                    rebEND
                );
              #endif
                End_Line(term);
                break;

              case '\0':
                assert(false); // plain escape handled earlier for clarity
                goto unrecognized;

              unrecognized:;
              default:
                Unrecognized_Key_Sequence(cp - 1);
                goto restart;
            }

            ++cp;
            continue;
        }

        // C0 control codes and Bash-inspired Shortcuts
        //
        // https://en.wikipedia.org/wiki/C0_and_C1_control_codes
        // https://ss64.com/bash/syntax-keyboard.html
        //
        switch (*cp) {
          case BS: // backspace (C0)
          case DEL: // delete (C0)
            Delete_Char(term, true);
            break;

          case CR: // carriage return (C0)
            if (cp[1] == LF)
                ++cp; // disregard the CR character, else treat as LF
            goto line_feed;

          line_feed:;
          case LF: // line feed (C0)
            WRITE_STR("\n");
            Store_Line(term);
            ++cp;
            goto line_end_reached;

          case 1: // CTRL-A, Beginning of Line (bash)
            Home_Line(term);
            break;

          case 2: // CTRL-B, Backward One Character (bash)
            Move_Cursor(term, -1);
            break;

          case 3: // CTRL-C, Interrupt (ANSI, <signal.h> is standard C)
            //
            // It's theoretically possible to clear the termios `c_lflag` ISIG
            // in order to receive literal Ctrl-C, but we don't want to get
            // involved at that level.  Using sigaction() on SIGINT and
            // causing EINTR is how we would like to be triggering HALT.
            //
            rebJumps(
                "FAIL {Unexpected literal Ctrl-C in console}",
                rebEND
            );

          case 4: // CTRL-D, Synonym for Cancel Input (Windows Terminal Garbage)
            //
            // !!! In bash this is "Delete Character Under the Cursor".  But
            // the Windows Terminal forces our hands to not use Escape for
            // canceling input.  See notes regarding ReadConsole() along with
            // ENABLE_LINE_INPUT.
            //
            // If one is forced to choose only one thing, it makes more sense
            // to make this compatible with the Windows console so there's
            // one shortcut you can learn that works on both platforms.
            // Though ideally it would be configurable--and it could be, if
            // the Windows version had to manage the edit buffer with as
            // much manual code as this POSIX version does.
            //
          #if 0
            Delete_Char(term, false);
            break
          #else
            goto return_blank;
          #endif

          case 5: // CTRL-E, End of Line (bash)
            End_Line(term);
            break;

          case 6: // CTRL-F, Forward One Character (bash)
            Move_Cursor(term, 1);
            break;

          default:
            Unrecognized_Key_Sequence(cp);
            goto restart;
        }

        ++cp;
    }

    goto restart;

  return_blank:
    //
    // INPUT has a display invariant that the author of the code expected
    // a newline to be part of what the user contributed.  To keep the visual
    // flow in the case of a cancellation key that didn't have a newline, we
    // have to throw one in.
    //
    WRITE_STR("\n");
    return rebBlank();

  return_halt:
    WRITE_STR("\n"); // see note above on INPUT's display invariant
    return nullptr;

  line_end_reached:
    // Not at end of input? Save any unprocessed chars:
    if (*cp != '\0') {
        if (LEN_BYTES(term->residue) + LEN_BYTES(cp) >= RESIDUE_LEN - 1) {
            //
            // avoid overrun
        }
        else
            strcat(s_cast(term->residue), cs_cast(cp));
    }

    // We could return the term buffer directly and allocate a new buffer.
    // But returning a copy lets the return result be a new allocation at the
    // exact size of the final input (e.g. paging through the history and
    // getting longer entries transiently won't affect the extra capacity of
    // what is ultimately returned.)
    //
    return rebValue("copy", term->buffer, rebEND);
}
