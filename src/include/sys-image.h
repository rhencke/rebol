//
//  File: %sys-image.h
//  Summary: {Definitions for IMAGE! Datatype}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
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
// !!! R3-Alpha's implementation of the IMAGE! datatype had several strange
// aspects--it tried to unify a 2-dimensional structure with the 1-dimensional
// indexing idea of a series.  This gave rise to various semantic ambiguities
// such as "what happens when you append red to a 1x1 image".  Do you get an
// error, a new column to make a 1x2 image, or a new row for a 2x1 image?
// How does the system handle IMAGE! values that have been advanced via
// NEXT or FIND to positions other than the head?
//
// https://github.com/rebol/rebol-issues/issues/801
//
// Ren-C's primary goals are to research and pin down fundamentals, where
// things like IMAGE! would be an extension through a user-defined type
// vs. being in the core.  So the main goal is to excise "weirdness" that
// comes from REB_IMAGE affecting builds that would not use it.
//

#define IMG_WIDE(s) \
    (MISC(s).area.wide)

#define IMG_HIGH(s) \
    (MISC(s).area.high)

#define VAL_IMAGE_WIDE(v) \
    IMG_WIDE(VAL_SERIES(v))

#define VAL_IMAGE_HIGH(v) \
    IMG_HIGH(VAL_SERIES(v))

#define VAL_IMAGE_HEAD(v) \
    SER_DATA_RAW(VAL_SERIES(v))

#define VAL_IMAGE_AT_HEAD(v,index) \
    (SER_DATA_RAW(VAL_SERIES(v)) + ((index) * 4))

// !!! The functions that take into account the current index position in the
// IMAGE!'s ANY-SERIES! payload are sketchy, in the sense that being offset
// into the data does not change the width or height...only the length when
// viewing the image as a 1-dimensional series.  This is not likely to make
// a lot of sense.

inline static REBYTE *VAL_IMAGE_AT(const REBCEL *v)
  { return SER_DATA_RAW(VAL_SERIES(v)) + (VAL_INDEX(v) * 4); }

#define VAL_IMAGE_LEN_AT(v) \
    VAL_LEN_AT(v)
