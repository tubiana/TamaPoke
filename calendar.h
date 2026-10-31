#pragma once
#include <stdint.h>
#include "dex.h"     // T_* for each event's egg type

// Calendar surprises: a handful of days in the year, each with its own
// decorations on the main screen, a sticker for being there, and eggs that
// lean towards a type while it lasts. Plus the player's birthday, which is set
// in OPTIONS and brings a present.
//
// Pure functions with no Arduino dependency, so the tests can sweep a whole
// year. The date comes from the RTC epoch, which this firmware keeps in LOCAL
// time (SETTINGS sets it to the wall clock), so day = epoch / 86400 is the
// local day and needs no timezone.

enum : uint8_t {
  EV_NONE = 0,
  EV_NEWYEAR, EV_VALENTINE, EV_POKEDAY, EV_SPRING, EV_SUMMER,
  EV_AUTUMN, EV_HALLOWEEN, EV_WINTER, EV_CHRISTMAS,
  EV_BIRTHDAY,                       // not in CAL_EVENTS: its date is the player's
  EV_COUNT
};

struct CalEvent {
  uint8_t ev;
  uint8_t month, dayFrom, dayTo;     // inclusive, all within one month
  uint8_t eggType;                   // T_NONE: no lean (NEW YEAR boosts shinies instead)
};

// A season starts on its equinox or solstice and lasts a week. Fixed dates:
// the real ones move by a day year to year, which nobody holding a toy minds.
// WINTER ends where CHRISTMAS begins, so no day is ever two events.
static const CalEvent CAL_EVENTS[] = {
  { EV_NEWYEAR,   1,  1,  1, T_NONE },
  { EV_VALENTINE, 2, 14, 14, T_FAIRY },
  { EV_POKEDAY,   2, 27, 27, T_ELECTRIC },   // Red and Green, 27 Feb 1996
  { EV_SPRING,    3, 20, 26, T_GRASS },
  { EV_SUMMER,    6, 21, 27, T_WATER },
  { EV_AUTUMN,    9, 22, 28, T_GROUND },
  { EV_HALLOWEEN,10, 25, 31, T_GHOST },
  { EV_WINTER,   12, 21, 23, T_ICE },
  { EV_CHRISTMAS,12, 24, 26, T_ICE },
};
#define CAL_EVENT_COUNT (sizeof(CAL_EVENTS) / sizeof(CAL_EVENTS[0]))

struct CalDate { uint16_t year; uint8_t month, day; };

// Days since 1970-01-01 to a civil date (Howard Hinnant's algorithm), exact
// for every date this device will ever see.
static inline CalDate calFromDays(uint32_t days) {
  int32_t z = (int32_t)days + 719468;
  int32_t era = z / 146097;
  uint32_t doe = (uint32_t)(z - era * 146097);
  uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int32_t y = (int32_t)yoe + era * 400;
  uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  uint32_t mp = (5 * doy + 2) / 153;
  CalDate c;
  c.day = (uint8_t)(doy - (153 * mp + 2) / 5 + 1);
  c.month = (uint8_t)(mp < 10 ? mp + 3 : mp - 9);
  c.year = (uint16_t)(y + (c.month <= 2));
  return c;
}

static inline uint8_t calMonthDays(uint8_t m, uint16_t y) {
  static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  if (m < 1 || m > 12) return 31;
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
  return D[m - 1];
}

// Is today the birthday? A 29 February birthday is kept on the 28th in the
// years that have no 29th, so it still comes every year.
static inline bool calIsBirthday(CalDate c, uint8_t bm, uint8_t bd) {
  if (!bm || !bd) return false;
  if (c.month != bm) return false;
  if (c.day == bd) return true;
  return bm == 2 && bd == 29 && c.day == 28 && calMonthDays(2, c.year) == 28;
}

// The event on this date, birthday first: a birthday on Halloween is a birthday.
static inline uint8_t calEventOn(CalDate c, uint8_t bm, uint8_t bd) {
  if (calIsBirthday(c, bm, bd)) return EV_BIRTHDAY;
  for (uint8_t i = 0; i < CAL_EVENT_COUNT; i++) {
    const CalEvent &e = CAL_EVENTS[i];
    if (c.month == e.month && c.day >= e.dayFrom && c.day <= e.dayTo) return e.ev;
  }
  return EV_NONE;
}

static inline uint8_t calEggType(uint8_t ev) {
  for (uint8_t i = 0; i < CAL_EVENT_COUNT; i++)
    if (CAL_EVENTS[i].ev == ev) return CAL_EVENTS[i].eggType;
  return T_NONE;
}
