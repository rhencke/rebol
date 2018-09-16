//
//  File: %sys-ext.h
//  Summary: "Extension Hook Point Definitions"
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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#if defined(EXT_DLL)
    //
    // EXT_DLL being defined indicates an "external extension".  Its entry
    // and shutdown functions have predictable names (RX_Init() and RX_Quit())
    // and are exported from the DLL.

  #if defined(REB_EXE)
    #define EXT_API EXTERN_C API_IMPORT // Hosting Rebol is an EXE
  #else
    #define EXT_API EXTERN_C API_EXPORT // Hosting Rebol is a DLL/LIB
  #endif

    // Just ignore the extension name parameter
    //
    #define RX_INIT_NAME(ext_name) RX_Init
    #define RX_QUIT_NAME(ext_name) RX_Quit
#else
    // If EXT_DLL is not defined, this is a "built-in extension".  It is
    // part of the exe or lib, and its startup and shutdown functions must be
    // distinguished by name from other extensions that are built-in.

    #define EXT_API EXTERN_C

    // *Don't* ignore the extension name parameter
    //
    #define RX_INIT_NAME(ext_name) RX_Init_##ext_name
    #define RX_QUIT_NAME(ext_name) RX_Quit_##ext_name
#endif

typedef int (*INIT_CFUNC)(REBVAL*, REBVAL*);
typedef int (*QUIT_CFUNC)(void);


//=//// EXTENSION MACROS //////////////////////////////////////////////////=//

#define DECLARE_EXT_INIT(ext_name) \
    EXT_API int RX_INIT_NAME(ext_name)(REBVAL *header, REBVAL *out)

#define DEFINE_EXT_INIT(ext_name,script_bytes,code) \
    EXT_API int RX_INIT_NAME(ext_name)(REBVAL *script, REBVAL *out) { \
        code \
        Init_Binary(script, Copy_Bytes(script_bytes, sizeof(script_bytes) - 1)); \
        return 0;\
    }

#define DEFINE_EXT_INIT_COMPRESSED(ext_name,script_bytes,code) \
    EXT_API int RX_INIT_NAME(ext_name)(REBVAL *script, REBVAL *out) { \
        code \
        /* binary does not have a \0 terminator */ \
        size_t utf8_size; \
        const int max = -1; \
        void *utf8 = rebGunzipAlloc( \
            &utf8_size, script_bytes, sizeof(script_bytes), max \
        ); \
        REBVAL *bin = rebRepossess(utf8, utf8_size); \
        Move_Value(script, bin); \
        rebRelease(bin); /* should just return the BINARY! REBVAL* */ \
        return 0;\
    }

#define DECLARE_EXT_QUIT(ext_name) \
    EXT_API int RX_QUIT_NAME(ext_name)(void)

#define DEFINE_EXT_QUIT(ext_name,code) \
    EXT_API int RX_QUIT_NAME(ext_name)(void) code


//=//// MODULE MACROS /////////////////////////////////////////////////////=//

#define DECLARE_MODULE_INIT(mod_name) \
    int Module_Init_##mod_name(REBVAL* out)

#define CALL_MODULE_INIT(mod_name) \
    Module_Init_##mod_name(out)

#define DECLARE_MODULE_QUIT(mod_name) \
    int Module_Quit_##mod_name(void)

#define CALL_MODULE_QUIT(mod_name) \
    Module_Quit_##mod_name()
