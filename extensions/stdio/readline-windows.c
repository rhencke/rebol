//
//  File: %readline-windows.c
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

#include <assert.h>
#include <stdint.h>
#include "reb-c.h"

#include "readline.h"  // will define REBOL_SMART_CONSOLE (if not C89)

#if defined(REBOL_SMART_CONSOLE)

#include <stdlib.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN  // trim down the Win32 headers
#include <windows.h>


//=//// REBOL INCLUDES + HELPERS //////////////////////////////////////////=//

#define xrebWord(cstr) \
    rebValue("lit", cstr)


//=//// CONFIGURATION /////////////////////////////////////////////////////=//

enum {
    BEL = 7,
    BS = 8,
    LF = 10,
    CR = 13,
    ESC = 27,
    DEL = 127
};


#define READ_BUF_LEN 64   // input events read at a time from console

struct Reb_Terminal_Struct {
    REBVAL *buffer;  // a TEXT! used as a buffer
    unsigned int pos;  // cursor position within the line

    INPUT_RECORD buf[READ_BUF_LEN];
    INPUT_RECORD *in;  // input record pointer
    INPUT_RECORD *in_tail;  // can't "null terminate", so track tail

    // Windows provides WINDOW_BUFFER_SIZE_EVENT so we are notified when the
    // width or height of the console changes.
    //
    DWORD columns;
    DWORD rows;

    DWORD original_mode;  // original console mode (restore on exit)

    // Windows streams a lot of events that need to be filtered/ignored, in
    // the midst of things like a PASTE (such as ctrl key being down and
    // repeated from the Ctrl-V).  To get decent performance, pastes must
    // be accrued and not done character-by-character in buffered mode, so
    // it does this by gathering up encoded text events and only sending the
    // TEXT! back when a new event is calculated.  We preserve that event
    // in the terminal state to return on the next call.
    //
    REBVAL *e_pending;
};

static HANDLE Stdin_Handle = nullptr;
static HANDLE Stdout_Handle = nullptr;


#define WRITE_CHAR(s) \
    do { \
        if (write(1, s, 1) == -1) { \
            /* Error here, or better to "just try to keep going"? */ \
        } \
    } while (0)

static void WRITE_UTF8(const unsigned char *utf8, size_t size)
{
    // Convert UTF-8 buffer to Win32 wide-char format for console.
    // When not redirected, the default seems to be able to translate
    // LF to CR LF automatically (assuming that's what you wanted).
    //
    // !!! We use this instead of rebSpellWide() because theoretically
    // this will handle high codepoint characters like emoji, which
    // in UTF-16 are more than one wide-char.  In practice Windows does
    // not seem to draw emoji in older Command Prompt or PowerShell, but
    // a new "Windows Terminal" from the app store supposedly does (if
    // you've installed a "Preview build" of Windows).
    //
    WCHAR *wchar_buf = cast(WCHAR*, malloc(size * 2 + 2));
    DWORD num_wchars = MultiByteToWideChar(
        CP_UTF8,
        0,
        cast(const char*, utf8),
        size,
        wchar_buf,
        size * 2 + 2
    );
    if (num_wchars > 0) {  // no error
        DWORD total_wide_chars;
        BOOL ok = WriteConsoleW(
            Stdout_Handle,
            wchar_buf,
            num_wchars,
            &total_wide_chars,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());
        UNUSED(total_wide_chars);
    }
    free(wchar_buf);
}


//=//// GLOBALS ///////////////////////////////////////////////////////////=//

static bool Term_Initialized = false;  // Terminal init was successful


inline static unsigned int Term_End(STD_TERM *t)
  { return rebUnboxInteger("length of", t->buffer); }

inline static unsigned int Term_Remain(STD_TERM *t)
  { return Term_End(t) - t->pos; }

// Older MSVC installations don't define SetConsoleMode()'s "extended flags"
// https://docs.microsoft.com/en-us/windows/console/setconsolemode
//
#if !defined(ENABLE_EXTENDED_FLAGS)
    #define ENABLE_EXTENDED_FLAGS 0x0080
#endif
#if !defined(ENABLE_QUICK_EDIT_MODE)
    #define ENABLE_QUICK_EDIT_MODE 0x0040
#endif
#if !defined(ENABLE_INSERT_MODE)
    #define ENABLE_INSERT_MODE 0x0020
#endif


//
//  Init_Terminal: C
//
// If possible, change the terminal to "raw" mode (where characters are
// received one at a time, as opposed to "cooked" mode where a whole line is
// read at once.)
//
STD_TERM *Init_Terminal(void)
{
    assert(not Term_Initialized);

    Stdin_Handle = GetStdHandle(STD_INPUT_HANDLE);
    Stdout_Handle = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode;
    GetConsoleMode(Stdin_Handle, &mode);

    // Windows offers its own "smart" line editor--with history management
    // and that handles backspaces/etc. which you get in ReadConsoleW() if
    // you have SetConsoleMode() with ENABLE_LINE_INPUT (the default mode).
    //
    // While truly "raw" input might seem nice, on Windows there are behaviors
    // like Cut/Copy/Paste/Find which are tied to keystrokes.  To get that
    // we have to use ENABLED_PROCESSED_INPUT, which prevents overriding
    // things like Ctrl-A to mean "jump to beginning of line".  We might
    // set it up so depending on the console mode these keys aren't used.
    //
    // We do not use ENABLE_ECHO_INPUT, because that would limit us to
    // always printing whatever was typed--and we want to choose if we do.
    //
    if (not SetConsoleMode(
        Stdin_Handle,
            ENABLE_PROCESSED_INPUT  // makes Copy, Paste, Find, etc. work
            | ENABLE_EXTENDED_FLAGS  // needed for QUICK_EDIT
            | ENABLE_QUICK_EDIT_MODE  // user can copy/paste
    )){
        return nullptr;
    }

    STD_TERM *t = cast(STD_TERM*, malloc(sizeof(STD_TERM)));

    t->original_mode = mode;

    t->buffer = rebValue("{}");
    rebUnmanage(t->buffer);

    t->in = t->in_tail = t->buf;  // start read() byte buffer out at empty

    t->pos = 0;  // start cursor position out at 0 (assured?)

    t->e_pending = nullptr;

    // Get the terminal dimensions (note we get events when resizes happen)
    // https://stackoverflow.com/a/12642749
    //
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        t->columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        t->rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    else {  // !!! Don't consider it a fatal error if size can't be acquired?
        t->columns = 0;
        t->rows = 0;
    }

    // !!! Ultimately, we want to be able to recover line history from a
    // file across sessions.  It makes more sense for the logic doing that
    // to be doing it in Rebol.  For starters, we just make it fresh.
    //
    Line_History = rebValue("[{}]");  // current line is empty string
    rebUnmanage(Line_History);  // allow Line_History to live indefinitely

    Term_Initialized = true;
    return t;
}


//
//  Term_Pos: C
//
// The STD_TERM is opaque, but it holds onto a buffer.
//
int Term_Pos(STD_TERM *t)
{
    return t->pos;
}


//
//  Term_Buffer: C
//
// This gives you a read-only perspective on the buffer.  You should not
// change it directly because doing so would not be in sync with the cursor
// position or what is visible on the display.  All changes need to go through
// the terminal itself.
//
REBVAL *Term_Buffer(STD_TERM *t)
{
    return rebValue("const", t->buffer);
}


//
//  Quit_Terminal: C
//
// Restore the terminal modes original entry settings,
// in preparation for exit from program.
//
void Quit_Terminal(STD_TERM *t)
{
    assert(Term_Initialized);

    SetConsoleMode(Stdin_Handle, t->original_mode);

    rebRelease(t->buffer);
    free(t);

    rebRelease(Line_History);
    Line_History = nullptr;

    Term_Initialized = false;
}


#if defined(NDEBUG)
    #define CHECK_INPUT_RECORDS(t) NOOP
#else
    static void Check_Input_Records_Debug(STD_TERM *t) {
        assert(t->in < t->in_tail);

        INPUT_RECORD *p = t->in;
        for (; p != t->in_tail; ++p) {
            if (p->EventType == KEY_EVENT)
                assert(p->Event.KeyEvent.wRepeatCount >= 1);
        }
    }

    #define CHECK_INPUT_RECORDS(t) Check_Input_Records_Debug(t)
#endif


//
//  Read_Input_Events_Interrupted: C
//
// Read the next "chunk" of console input events into the buffer.
//
// !!! Note that if Emoji is supported, it may be that they come in as two
// input events (surrogate pair?)...which means they might split across
// two buffer reads.  Look into this
//
static bool Read_Input_Events_Interrupted(STD_TERM *t)
{
    assert(t->in == t->in_tail);  // Don't read more if buffer not consumed
    assert(t->e_pending == nullptr);  // Don't read if event is pending

    DWORD num_events;
    if (not ReadConsoleInput( 
        Stdin_Handle,  // input buffer handle 
        t->buf,  // buffer to read into 
        READ_BUF_LEN - 1,  // size of read buffer 
        &num_events
    )){
        rebFail_OS (GetLastError());
    }
    assert(num_events != 0);  // Should be blocking (see PeekConsoleInput)

    t->in_tail = t->buf + num_events;
    t->in = t->buf;

  #if !defined(NDEBUG)
    t->in_tail->EventType = MENU_EVENT;
    t->in_tail->Event.MenuEvent.dwCommandId = 10203;
  #endif
 
    CHECK_INPUT_RECORDS(t);
    return false;  // not interrupted (note we could return `len` if needed)
}


//
//  Write_Char: C
//
// Write out repeated number of chars.
//
void Write_Char(uint32_t c, int n)
{
    if (c > 0xFFFF)
        rebJumps ("fail {Not yet working with codepoints >0xFFFF on Windows");

    WCHAR c_wide = c;

    for (; n > 0; n--) {
        DWORD total_wide_chars;
        BOOL ok = WriteConsoleW(
            Stdout_Handle,
            &c_wide,
            1,
            &total_wide_chars,
            0
        );
        if (not ok)
            rebFail_OS (GetLastError());
        UNUSED(total_wide_chars);
    }
}


//
//  Clear_Line_To_End: C
//
// Clear all the chars from the current position to the end.
// Reset cursor to current position.
//
void Clear_Line_To_End(STD_TERM *t)
{
    int num_codepoints_to_end = Term_Remain(t);
    rebElide("clear skip", t->buffer, rebI(t->pos));

    Write_Char(' ', num_codepoints_to_end);  // wipe to end of line...
    Write_Char(BS, num_codepoints_to_end);  // ...then return to position
}


//
//  Term_Seek: C
//
// Reset cursor to home position.
//
void Term_Seek(STD_TERM *t, unsigned int pos)
{
    int delta = (pos < t->pos) ? -1 : 1;
    while (pos != t->pos)
        Move_Cursor(t, delta);
}


//
//  Show_Line: C
//
// Refresh a line from the current position to the end.
// Extra blanks can be specified to erase chars off end.
// If blanks is negative, stay at end of line.
// Reset the cursor back to current position.
//
static void Show_Line(STD_TERM *t, int blanks)
{
    // Clip bounds
    //
    unsigned int end = Term_End(t);
    if (t->pos > end)
        t->pos = end;

    if (blanks >= 0) {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            "skip", t->buffer, rebI(t->pos)
        );

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);
    }
    else {
        size_t num_bytes;
        unsigned char *bytes = rebBytes(&num_bytes,
            t->buffer
        );

        WRITE_UTF8(bytes, num_bytes);
        rebFree(bytes);

        blanks = -blanks;
    }

    Write_Char(' ', blanks);
    Write_Char(BS,  blanks);  // return to original position or end

    // We want to write as many backspace characters as there are *codepoints*
    // in the buffer to end of line.
    //
    Write_Char(BS, Term_Remain(t));
}


//
//  Delete_Char: C
//
// Delete a char at the current position. Adjust end position.
// Redisplay the line. Blank out extra char at end.
//
void Delete_Char(STD_TERM *t, bool back)
{
    unsigned int end = Term_End(t);

    if (t->pos == end and not back)
        return;  // Ctrl-D (forward-delete) at end of line

    if (t->pos == 0 and back)
        return;  // backspace at beginning of line

    if (back)
        --t->pos;

    if (end > 0) {
        rebElide("remove skip", t->buffer, rebI(t->pos));

        if (back)
            Write_Char(BS, 1);

        Show_Line(t, 1);
    }
    else
        t->pos = 0;
}


//
//  Move_Cursor: C
//
// Move cursor right or left by one char.
//
void Move_Cursor(STD_TERM *t, int count)
{
    if (count < 0) {
        //
        // "backspace" in TERMIOS lets you move the cursor left without
        //  knowing what character is there and without overwriting it.
        //
        if (t->pos > 0) {
            --t->pos;
            Write_Char(BS, 1);
        }
    }
    else {
        // Moving right without affecting a character requires writing the
        // character you know to be already there (via the buffer).
        //
        unsigned int end = Term_End(t);
        if (t->pos < end) {
            size_t encoded_size;
            unsigned char *encoded_char = rebBytes(&encoded_size,
                "to binary! pick", t->buffer, rebI(t->pos + 1)
            );
            WRITE_UTF8(encoded_char, encoded_size);
            rebFree(encoded_char);

            t->pos += 1;
        }
    }
}


//
//  Try_Get_One_Console_Event: C
//
REBVAL *Try_Get_One_Console_Event(STD_TERM *t, bool buffered)
{
    REBVAL *e = nullptr;  // *unbuffered* event to return
    REBVAL *e_buffered = nullptr;  // buffered event

    if (t->e_pending) {
        e = t->e_pending;
        t->e_pending = nullptr;
        return e;
    }

  start_over:
    assert(not e and not t->e_pending);
    assert(
        not e_buffered
        or (buffered and rebDid("text?", e_buffered))
    );

    if (t->in == t->in_tail) {  // no residual events from prior read
        if (e_buffered)
            return e_buffered;  // pass anything we gathered so far first

        if (Read_Input_Events_Interrupted(t))
            return rebVoid();  // signal a HALT

        assert(t->in != t->in_tail);
    }

    if (t->in->EventType == WINDOW_BUFFER_SIZE_EVENT) {
        t->columns = t->in->Event.WindowBufferSizeEvent.dwSize.X;
        t->rows = t->in->Event.WindowBufferSizeEvent.dwSize.Y;
        ++t->in;
    }
    else if (t->in->EventType == FOCUS_EVENT) {
        //
        // Ignore focus events (for now)...a richer console might offer
        // these events if available.
        //
        ++t->in;
    }
    else if (t->in->EventType == MENU_EVENT) {
      #if !defined(NDEBUG)
        //
        // We use a special menu event in the debug build to "poison" the
        // tail and notice overruns of t->in_tail.
        //
        assert(t->in->Event.MenuEvent.dwCommandId != 10203);
      #endif

        // Ignore menu events.  They are likely not interesting, because the
        // console runs in a separate process and has a fixed menu.  So you
        // can't add new menu items and get which one was clicked (Raymond
        // Chen of MS Windows fame has said "even if you could get it to
        // work, it's not supported".)
        //
        ++t->in;
    }
    else if (
        t->in->EventType == KEY_EVENT and not t->in->Event.KeyEvent.bKeyDown
    ){
        // We ignore key up events for now, but an unbuffered mode might
        // want to give them back.
        //
        ++t->in;
    }
    else if (
        t->in->EventType == KEY_EVENT
        and t->in->Event.KeyEvent.uChar.UnicodeChar >= 32  // 32 is space
        and t->in->Event.KeyEvent.uChar.UnicodeChar != 127  // 127 is DEL
    ){

    //=//// ASCII printable character or UTF-8 ////////////////////////////=//
        //
        // https://en.wikipedia.org/wiki/ASCII
        // https://en.wikipedia.org/wiki/UTF-8

        assert(t->in->Event.KeyEvent.wRepeatCount > 0);

        // !!! It's not clear how Windows Terminal intends to handle high
        // codepoints such as Emoji; they are perhaps "surrogate pair"
        // events, and thus could be split across two different event
        // reads the way UTF-8 can get split on POSIX.  Review.
        //
        uint32_t codepoint = t->in->Event.KeyEvent.uChar.UnicodeChar;

        // The terminal events may contain a repeat count for a key that
        // is pressed multiple times.  If this is the case, we do not
        // advance the input record pointer...but decrement the count.
        //
        assert(t->in->Event.KeyEvent.wRepeatCount > 0);
        if (--t->in->Event.KeyEvent.wRepeatCount == 0)
            ++t->in;  // "consume" the event if all the repeats are done

        if (not buffered) {  // one CHAR! at a time
            e = rebChar(codepoint);
        }
        else {
            if (e_buffered)
                rebElide("append", e_buffered, rebR(rebChar(codepoint)));
            else
                e_buffered = rebValue("to text!", rebR(rebChar(codepoint)));
        }
    }
    else if (
        t->in->EventType == KEY_EVENT
        and t->in->Event.KeyEvent.bKeyDown
    ){
        static struct {
           WORD code;
           const char *name;
        } keynames[] = {
            // VK_ESCAPE handled specially below (cancels pending input)
            { VK_LEFT, "left" },
            { VK_RIGHT, "right" },
            { VK_UP, "up" },
            { VK_DOWN, "down" },
            { VK_HOME, "home" },
            { VK_END, "end" },
            { VK_CLEAR, "clear" },
            { VK_TAB, "tab" },
            { VK_BACK, "backspace" },
            { VK_DELETE, "delete" },
            { 0, nullptr }
        };

        const WCHAR wchar = t->in->Event.KeyEvent.uChar.UnicodeChar;
        const WORD vkey = t->in->Event.KeyEvent.wVirtualKeyCode;

        if (wchar == '\n' or vkey == VK_RETURN) {
            e = rebChar('\n');
        }
        if (not e and vkey == VK_ESCAPE) {
            //
            // We want escape to halt any pending input.  Perhaps this should
            // not just clear the console buffer, but peek and repeatedly
            // clear until it settles.
            //
            t->in_tail = t->buf;
            t->in = t->buf;
            e = xrebWord("escape");
        }
        if (not e) {
            int i;
            for (i = 0; keynames[i].name != nullptr; ++i) {
                if (keynames[i].code == vkey) {
                    e = xrebWord(keynames[i].name);
                    break;
                }
            }
        }
        if (not e and (wchar >= 1 and wchar <= 26)) {  // Ctrl-A, Ctrl-B, etc.
            e = rebValue(
                "as word! unspaced [",
                    "{ctrl-}", rebR(rebChar(wchar - 1 + 'a')),
                "]"
            );
        }

        assert(t->in->Event.KeyEvent.wRepeatCount > 0);
        if (--t->in->Event.KeyEvent.wRepeatCount == 0)
            ++t->in;  // consume event if no more repeats
    }
    else {
        ++t->in;  // some generic other event, so throw it out
    }

    if (e != nullptr) {  // a non-buffered event was produced
        if (e_buffered) {  // but we have pending buffered text...
            t->e_pending = e;  // ...so make the non-buffered event pending
            return e_buffered;  // and return the buffer first
        }

        return e;  // if no buffer in waiting, return non-buffered event
    }

    // !!! This will screw up the repeated key count strategy
    //
    if (t->in != t->in_tail)
        ++t->in;  // Each branch has the option to advance or not

    goto start_over;
}


//
//  Term_Insert_Char: C
//
static void Term_Insert_Char(STD_TERM *t, uint32_t c)
{
    if (c == BS) {
        if (t->pos > 0) {
            rebElide("remove skip", t->buffer, rebI(t->pos));
            --t->pos;
            Write_Char(BS, 1);
        }
    }
    else if (c == LF) {
        //
        // !!! Currently, if a newline actually makes it into the terminal
        // by asking to put it there, you see a newline visually, but the
        // buffer content is lost.  You can't then backspace over it.  So
        // perhaps obviously, the terminal handling code when it gets a
        // LF *key* as input needs to copy the buffer content out before it
        // decides to ask for the LF to be output visually.
        //
        rebElide("clear", t->buffer);
        t->pos = 0;
        Write_Char(LF, 1);
    }
    else {
        REBVAL *codepoint = rebChar(c);

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            "insert skip", t->buffer, rebI(t->pos), codepoint,
            codepoint  // fold returning of codepoint in with insertion
        );
        WRITE_UTF8(encoded, encoded_size);
        rebFree(encoded);

        rebRelease(codepoint);

        ++t->pos;
    }
}


//
//  Term_Insert: C
//
// Inserts a Rebol value (TEXT!, CHAR!) at the current cursor position.
// This is made complicated because we have to sync our internal knowledge
// with what the last line in the terminal is showing...which means mirroring
// its logic regarding cursor position, newlines, backspacing.
//
void Term_Insert(STD_TERM *t, const REBVAL *v) {
    if (rebDid("char?", v)) {
        Term_Insert_Char(t, rebUnboxChar(v));
        return;
    }

    int len = rebUnboxInteger("length of", v);

    if (rebDid("find", v, "backspace")) {
        //
        // !!! The logic for backspace and how it interacts is nit-picky,
        // and "reaches out" to possibly edit the existing buffer.  There's
        // no particularly easy way to handle this, so for now just go
        // through a slow character-by-character paste.  Assume this is rare.
        //
        int i;
        for (i = 1; i <= len; ++i)
            Term_Insert_Char(t, rebUnboxChar("pick", v, rebI(i)));
    }
    else {  // Finesse by doing one big write
        //
        // Systems may handle tabs differently, but we want our buffer to
        // have the right number of spaces accounted for.  Just transform.
        //
        REBVAL *v_no_tab = rebValue(
            "if find", v, "tab [",
                "replace/all copy", v, "tab", "{    }"
            "]"
        );

        size_t encoded_size;
        unsigned char *encoded = rebBytes(&encoded_size,
            v_no_tab ? v_no_tab : v
        );

        rebRelease(v_no_tab);  // null-tolerant

        // Go ahead with the OS-level write, in case it can do some processing
        // of that asynchronously in parallel with the following Rebol code.
        //
        WRITE_UTF8(encoded, encoded_size);
        rebFree(encoded);

        REBVAL *v_last_line = rebValue("next try find-last", v, "newline");

        // If there were any newlines, then whatever is in the current line
        // buffer will no longer be there.
        //
        if (v_last_line) {
            rebElide("clear", t->buffer);
            t->pos = 0;
        }

        const REBVAL *insertion = v_last_line ? v_last_line : v;

        t->pos += rebUnboxInteger(
            "insert skip", t->buffer, rebI(t->pos), insertion,
            "length of", insertion
        );

        rebRelease(v_last_line);  // null-tolerant
    }

    Show_Line(t, 0);
}


//
//  Term_Beep: C
//
// Trigger some beep or alert sound.
//
void Term_Beep(STD_TERM *t)
{
    UNUSED(t);
    Write_Char(BEL, 1);
}

#endif  // end guard against readline in pre-C99 compilers (would need rebEND)
