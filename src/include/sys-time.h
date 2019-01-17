//
//  File: %sys-time.h
//  Summary: {Definitions for the TIME! and DATE! Datatypes}
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
//


// All dates have REBYMD information in their ->extra field, but not all
// of them also have associated time information.  This value for the nano
// means there is no time.
//
#define NO_DATE_TIME INT64_MIN

inline static bool Does_Date_Have_Time(const REBCEL *v)
{
    assert(CELL_KIND(v) == REB_DATE);
    return v->payload.time.nanoseconds != NO_DATE_TIME;
}

// There is a difference between a time zone of 0 (explicitly GMT) and
// choosing to be an agnostic local time.  This bad value means no time zone.
//
#define NO_DATE_ZONE -64

inline static bool Does_Date_Have_Zone(const REBCEL *v)
{
    assert(CELL_KIND(v) == REB_DATE);
    return v->extra.date.date.zone != NO_DATE_ZONE; // 7-bit field
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  TIME! (and time component of DATE!s that have times)
//
//=////////////////////////////////////////////////////////////////////////=//

inline static REBI64 VAL_NANO(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_TIME or Does_Date_Have_Time(v));
    return v->payload.time.nanoseconds;
}

#define SECS_TO_NANO(seconds) \
    (cast(REBI64, seconds) * 1000000000L)

#define MAX_SECONDS \
    ((cast(REBI64, 1) << 31) - 1)

#define MAX_HOUR \
    (MAX_SECONDS / 3600)

#define MAX_TIME \
    (cast(REBI64, MAX_HOUR) * HR_SEC)

#define NANO 1.0e-9

#define SEC_SEC \
    cast(REBI64, 1000000000L)

#define MIN_SEC \
    (60 * SEC_SEC)

#define HR_SEC \
    (60 * 60 * SEC_SEC)

#define SEC_TIME(n) \
    ((n) * SEC_SEC)

#define MIN_TIME(n) \
    ((n) * MIN_SEC)

#define HOUR_TIME(n) \
    ((n) * HR_SEC)

#define SECS_FROM_NANO(n) \
    ((n) / SEC_SEC)

#define VAL_SECS(n) \
    (VAL_NANO(n) / SEC_SEC)

#define DEC_TO_SECS(n) \
    cast(REBI64, ((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400

#define TIME_IN_DAY \
    SEC_TIME(cast(REBI64, SECS_IN_DAY))

inline static REBVAL *Init_Time_Nanoseconds(RELVAL *v, REBI64 nanoseconds) {
    RESET_CELL(v, REB_TIME);
    v->payload.time.nanoseconds = nanoseconds;
    return cast(REBVAL*, v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATE!
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_DATE(v) \
    ((v)->extra.date)

#define MAX_YEAR 0x3fff

#define VAL_YEAR(v) \
    ((v)->extra.date.date.year)

#define VAL_MONTH(v) \
    ((v)->extra.date.date.month)

#define VAL_DAY(v) \
    ((v)->extra.date.date.day)


inline static int VAL_ZONE(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_DATE and Does_Date_Have_Zone(v));
    return v->extra.date.date.zone;
}

inline static void INIT_VAL_ZONE(RELVAL *v, int zone) {
    assert(IS_DATE(v));
    assert(zone != NO_DATE_ZONE);
    v->extra.date.date.zone = zone;
}

#define ZONE_MINS 15

#define ZONE_SECS \
    (ZONE_MINS * 60)

#define MAX_ZONE \
    (15 * (60 / ZONE_MINS))
