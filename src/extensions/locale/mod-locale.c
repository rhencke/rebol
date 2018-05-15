//
//  File: %mod-locale.c
//  Summary: "Native Functions for spawning and controlling processes"
//  Section: Extension
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
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

#ifdef TO_WINDOWS
    #include <windows.h>
#endif
#include <locale.h>

// IS_ERROR might be defined in winerror.h and reb-types.h
#ifdef IS_ERROR
#undef IS_ERROR
#endif

#include "sys-core.h"
#include "sys-ext.h"

#include "tmp-mod-locale-first.h"


//
//  locale: native/export [
//      "Get locale specific information"
//      category [word!]
//          {Language: English name of the language,
//          Territory: English name of the country/region,
//          Language*: Full localized primary name of the language
//          Territory*: Full localized name of the country/region}
//  ]
//  new-errors: [
//      invalid-category: [{Invalid locale category:} :arg1]
//  ]
//
REBNATIVE(locale)
//
{
  #ifdef TO_WINDOWS
    LOCALE_INCLUDE_PARAMS_OF_LOCALE;

    REBVAL *cat = ARG(category);

    LCTYPE type = rebUnbox(
        "select [",
            "language", rebI(LOCALE_SENGLANGUAGE),
            "language*", rebI(LOCALE_SNATIVELANGNAME),
            "territory", rebI(LOCALE_SENGCOUNTRY),
            "territory*", rebI(LOCALE_SCOUNTRY),
        "]", cat, "else [",
            "fail [{Invalid locale category:}", rebUneval(cat), "]",
        "]", END // !!! review using fail with ID-based errors
    );

    // !!! MS docs say: "For interoperability reasons, the application should
    // prefer the GetLocaleInfoEx function to GetLocaleInfo because Microsoft
    // is migrating toward the use of locale names instead of locale
    // identifiers for new locales. Any application that runs only on Windows
    // Vista and later should use GetLocaleInfoEx."
    //
    int len_plus_term = GetLocaleInfo(0, type, 0, 0); // fetch needed length

    WCHAR *buffer = rebAllocN(WCHAR, len_plus_term);

    int len_check = GetLocaleInfo(0, type, buffer, len_plus_term); // now get
    assert(len_check == len_plus_term);
    UNUSED(len_check);

    REBVAL *str = rebLengthedTextW(buffer, len_plus_term - 1);
    rebFree(buffer);

    Move_Value(D_OUT, str);
    rebRelease(str);

    return R_OUT;
  #else
    UNUSED(frame_);
    fail ("LOCALE not implemented natively for non-Windows");
  #endif
}


//
//  setlocale: native/export [
//
//  {Set/Get current locale, just a simple wrapper around C version}
//
//      return: [text! blank!]
//      category [word!]
//      value [text!]
//  ]
//  new-errors: [
//  ]
//
REBNATIVE(setlocale)
{
    LOCALE_INCLUDE_PARAMS_OF_SETLOCALE;

    // Some locales are GNU extensions, and only included via #ifdef
    //
    // http://man7.org/linux/man-pages/man7/locale.7.html
    //
    REBVAL *map = rebRun(
        "make map! [",
            "all", rebI(LC_ALL),

          #if defined(LC_ADDRESS)
            "address", rebI(LC_ADDRESS),
          #endif

            "collate", rebI(LC_COLLATE),
            "ctype", rebI(LC_CTYPE),

          #ifdef LC_IDENTIFICATION
            "identification", rebI(LC_IDENTIFICATION),
          #endif

          #ifdef LC_MEASUREMENT
            "measurement", rebI(LC_MEASUREMENT),
          #endif

          #ifdef LC_MESSAGES
            "messages", rebI(LC_MESSAGES),
          #endif

            "monetary", rebI(LC_MONETARY),

          #ifdef LC_NAME
            "name", rebI(LC_NAME),
          #endif

            "numeric", rebI(LC_NUMERIC),

          #ifdef LC_PAPER
            "paper", rebI(LC_PAPER),
          #endif

          #ifdef LC_TELEPHONE
            "telephone", rebI(LC_TELEPHONE),
          #endif

            "time", rebI(LC_TIME),
        "]", END
    );

    int cat = rebUnbox("select", map, ARG(category), "else [-1]", END);
    rebRelease(map);

    if (cat == -1)
        fail (Error(RE_EXT_LOCALE_INVALID_CATEGORY, ARG(category), END));

    char *value_utf8 = rebSpellAlloc(ARG(value), END);
    const char *result = setlocale(cat, value_utf8);
    rebFree(value_utf8);

    if (not result)
        return R_BLANK;

    REBVAL *str = rebText(result);
    Move_Value(D_OUT, str);
    rebRelease(str);
    return R_OUT;
}

#include "tmp-mod-locale-last.h"
