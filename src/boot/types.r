REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Rebol datatypes and their related attributes"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Developers
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This table is used to make C defines and intialization tables.

        name        - name of datatype (generates words)
        class       - how type actions are dispatched (T_type)
        path        - it supports various path forms
        make        - it can be made with #[datatype] method
        typesets    - what typesets the type belongs to

        What is in the table can be `+` to mean the method exists and has the
        same name as the type (e.g. MF_Blank() if type is BLANK!)

        If it is `*` then the method uses a common dispatcher for the type,
        so (e.g. PD_Array()) even if the type is BLOCK!)

        If it is `?` then the method is loaded dynamically by an extension, and
        unavailable otherwise and uses e.g. T_Unhooked()

        If it is `-` then it is not available at all, and will substitute with
        an implementation that fails, e.g. CT_Fail()

        Note that if there is `somename` in the class column, that means you
        will find the ACTION! dispatch for that type in `REBTYPE(Somename)`.
    }
    Macros: {
        /*
         * ORDER-DEPENDENT TYPE MACROS, e.g. ANY_BLOCK_KIND() or IS_BINDABLE()
         *
         * These macros embed specific knowledge of the type ordering.  They
         * are specified in %types.r, so anyone changing the order of types is
         * more likely to notice the impact, and adjust them.
         *
         * !!! Review how these might be auto-generated from the table.
         */

        /*
         * Note that an "in-situ" QUOTED! (not a REB_QUOTED kind byte, but
         * using larger REB_MAX values) is bindable if the cell it's overlaid
         * into is bindable.  It has to handle binding exactly as it would if
         * it were not a literal form.
         *
         * Actual REB_QUOTEDs (used for higher escape values) have to use a
         * separate cell for storage.  The REB_QUOTED type is in the range
         * of enum values that report bindability, even if it's storing a type
         * that uses the ->extra field for something else.  This is mitigated
         * by putting nullptr in the binding field of the REB_QUOTED portion
         * of the cell, instead of mirroring the ->extra field of the
         * contained cell...so it comes off as "specified" in those cases.
         */

        #define Is_Bindable(v) \
            (CELL_KIND_UNCHECKED(v) < REB_LOGIC) /* gets checked elsewhere */

        #define Not_Bindable(v) \
            (CELL_KIND_UNCHECKED(v) >= REB_LOGIC) /* gets checked elsewhere */

        /*
         * Testing for QUOTED! is special, as it isn't just the REB_QUOTED
         * type, but also multiplexed as values > REB_64.  So it costs more.
         */

        inline static bool IS_QUOTED_KIND(REBYTE k)
            { return k == REB_QUOTED or k >= REB_64; }

        #define IS_QUOTED(v) \
            IS_QUOTED_KIND(KIND_BYTE(v))

        /* Type categories.
         */

        #define ANY_VALUE(v) \
            (KIND_BYTE(v) != REB_MAX_NULLED)

        inline static bool ANY_SCALAR_KIND(REBYTE k)
            { return k >= REB_LOGIC and k <= REB_DATE; }

        #define ANY_SCALAR(v) \
            ANY_SCALAR_KIND(KIND_BYTE(v))

        inline static bool ANY_SERIES_KIND(REBYTE k)
           { return k >= REB_PATH and k <= REB_VECTOR; }

        #define ANY_SERIES(v) \
            ANY_SERIES_KIND(KIND_BYTE(v))

        inline static bool ANY_STRING_KIND(REBYTE k)
            { return k >= REB_TEXT and k <= REB_TAG; }

        #define ANY_STRING(v) \
            ANY_STRING_KIND(KIND_BYTE(v))

        inline static bool ANY_BINSTR_KIND(REBYTE k)
            { return k >= REB_BINARY and k <= REB_TAG; }

        #define ANY_BINSTR(v) \
            ANY_BINSTR_KIND(KIND_BYTE(v))

        inline static bool ANY_ARRAY_KIND(REBYTE k)
            { return k >= REB_PATH and k <= REB_BLOCK; }

        #define ANY_ARRAY(v) \
            ANY_ARRAY_KIND(KIND_BYTE(v))

        inline static bool ANY_WORD_KIND(REBYTE k)
            { return k >= REB_WORD and k <= REB_ISSUE; }

        #define ANY_WORD(v) \
            ANY_WORD_KIND(KIND_BYTE(v))

        inline static bool ANY_PATH_KIND(REBYTE k)
            { return k >= REB_PATH and k <= REB_GET_PATH; }

        #define ANY_PATH(v) \
            ANY_PATH_KIND(KIND_BYTE(v))

        inline static bool ANY_CONTEXT_KIND(REBYTE k)
            { return k >= REB_OBJECT and k <= REB_PORT; }

        #define ANY_CONTEXT(v) \
            ANY_CONTEXT_KIND(KIND_BYTE(v))

        /* !!! There was an IS_NUMBER() macro defined in R3-Alpha which was
         * REB_INTEGER and REB_DECIMAL.  But ANY-NUMBER! the typeset included
         * PERCENT! so this adds that and gets rid of IS_NUMBER()
         */
        inline static bool ANY_NUMBER_KIND(REBYTE k) {
            return k == REB_INTEGER or k == REB_DECIMAL or k == REB_PERCENT;
        }

        #define ANY_NUMBER(v) \
            ANY_NUMBER_KIND(KIND_BYTE(v))

        /* !!! Being able to locate inert types based on range *almost* works,
         * but REB_ISSUE and REB_REFINEMENT want to be picked up as ANY-WORD!.
         * This trick will have to be rethought, esp if words and strings
         * get unified, but it's here to show how choosing these values
         * carefully can help with speeding up tests.
         */
        inline static bool ANY_INERT_KIND(REBYTE k) {
            return (k >= REB_BLOCK and k <= REB_BLANK)
                or k == REB_ISSUE or k == REB_REFINEMENT;
        }

        #define ANY_INERT(v) \
            ANY_INERT_KIND(KIND_BYTE(v))

        /* Doing a SET-WORD! or SET-PATH!, or a plain SET assignment, does
         * not generally tolerate either voids or nulls.  Review if some
         * optimization could be made to test both at once more quickly
         * (putting them at 1 and 2, perhaps then < 3 could be the test).
         */
        inline static bool IS_NULLED_OR_VOID_KIND(REBYTE k)
            { return k == REB_MAX_NULLED or k == REB_VOID; }

        #define IS_NULLED_OR_VOID(v) \
            IS_NULLED_OR_VOID_KIND(KIND_BYTE(v))

        /* This is another kind of test that might have some way to speed up
         * based on types or bits, or as an alternate to another speedup if
         * it turns out to be more common.
         */
        inline static bool IS_NULLED_OR_BLANK_KIND(REBYTE k)
            { return k == REB_MAX_NULLED or k == REB_BLANK; }

        #define IS_NULLED_OR_BLANK(v) \
            IS_NULLED_OR_BLANK_KIND(KIND_BYTE(v))
    }
]


[name       description  ; used by HELP
            class       path    make    mold    typesets]  ; makes TS_XXX

; Note: 0 is reserved for an array terminator (REB_0), and not a "type"

action      "an invokable Rebol subroutine"  ;  https://forum.rebol.info/t/596
            action      +       +       +       []

; ===========================================================================
; ANY-WORD!, order matters, e.g. ANY_WORD() uses >= REB_WORD and <= REB_ISSUE

word        "word (symbol or variable)"
            word        *       *       +       [word]

set-word    "definition of a word's value"
            word        *       *       +       [word]

get-word    "the value of a word (variable)"
            word        *       *       +       [word]

refinement  "variation of meaning or location"
            word        *       *       +       [word]

issue       "identifying marker word"
            word        *       *       +       [word]

; ===========================================================================
; QUOTED! is "bindable", but nulls binding if it contains an unbindable type

quoted     "quoted container"
            quoted       +       +       -      [quoted]

; ===========================================================================
; ANY-ARRAY!, order matters (contiguous with ANY-SERIES below matters!)

path        "refinements to functions, objects, files"
            array       *       *       *       [series path array]

set-path    "definition of a path's value"
            array       *       *       *       [series path array]

get-path    "the value of a path"
            array       *       *       *       [series path array]

group       "array that evaluates expressions as an isolated group"
            array       *       *       *       [series array]

; -- start of inert bindable types (that aren't refinement! and issue!) --

block       "array of values that blocks evaluation unless DO is used"
            array       *       *       *       [series array]

; ===========================================================================
; ANY-SERIES!, order matters, and contiguous with ANY-ARRAY above matters

binary      "string series of bytes"
            string      *       *       +       [series]

text        "text string series of characters"
            string      *       *       *       [series string]

file        "file name or path"
            string      *       *       *       [series string]

email       "email address"
            string      *       *       *       [series string]

url         "uniform resource locator or identifier"
            string      *       *       *       [series string]

tag         "markup string (HTML or XML)"
            string      *       *       *       [series string]

; ^-------------------- end of ANY-STRING! types ----------------------------^

; v--------------- continue non-string ANY-SERIES! types --------------------v

bitset      "set of bit flags"
            bitset      +       +       +       []

image       "RGB image with alpha channel"
            image       +       +       +       [series]

vector      "high performance arrays (single datatype)"
            vector      +       +       +       [series]

map         "name-value pairs (hash associative)"
            map         +       +       +       []

varargs     "evaluator position for variable numbers of arguments"
            varargs     +       +       +       []

object      "context of names with values"
            context     *       *       *       [context]

module      "loadable context of code and data"
            context     *       *       *       [context]

error       "error context with id, arguments, and stack origin"
            context     *       +       +       [context]

frame       "arguments and locals of a specific action invocation"
            context     *       +       *       [context]

port        "external series, an I/O channel"
            port        context +       context [context]

; ^-------- Everything above is a "bindable" type, see Is_Bindable() --------^

; v------- Everything below is an "unbindable" type, see Is_Bindable() ------v

; scalars

logic       "boolean true or false"
            logic       -       +       +       []

integer     "64 bit integer"
            integer     -       +       +       [number scalar]

decimal     "64bit floating point number (IEEE standard)"
            decimal     -       *       +       [number scalar]

percent     "special form of decimals (used mainly for layout)"
            decimal     -       *       +       [number scalar]

money       "high precision decimals with denomination (opt)"
            money       -       +       +       [scalar]

char        "8bit and 16bit character"
            char        -       +       +       [scalar]

pair        "two dimensional point or size"
            pair        +       +       +       [scalar]

tuple       "sequence of small integers (colors, versions, IP)"
            tuple       +       +       +       [scalar]

time        "time of day or duration"
            time        +       +       +       [scalar]

date        "day, month, year, time of day, and timezone"
            date        +       +       +       []

; type system

datatype    "type of datatype"
            datatype    -       +       +       []

typeset     "set of datatypes"
            typeset     -       +       +       []

; things likely to become user-defined types or extensions

gob         "graphical object"
            gob         +       +       +       []

event       "user interface event (efficiently sized)"
            event       +       +       +       []

handle      "arbitrary internal object or value"
            handle      -       -       +       []

struct      "native structure definition"
            ?           ?       ?       ?       []

library     "external library reference"
            library     -       +       +       []

; ===========================================================================
; "unit types" https://en.wikipedia.org/wiki/Unit_type

blank       "placeholder unit type which also is conditionally false"
            unit        +       -       +       []

; -- end of inert unbindable types --

bar         "expression evaluation barrier"
            unit        -       -       +       []

void        "signal returned by actions that have no result"
            unit        -       -       +       []

; Note that the "null?" state has no associated NULL! datatype.  Internally
; it uses REB_MAX, but like the REB_0 it stays off the type map.  It is
; right next to void so that IS_NULLED_OR_VOID() can test for both with >=
; REB_VOID...
