#include "waste.h"
#include <stdint.h>

namespace {

enum HolidayRule : uint8_t { HR_FIXED, HR_NTH, HR_LAST };

// HR_FIXED: n = day of month.  HR_NTH: the n-th `wday` of the month.  HR_LAST: the last `wday`.
// wday: 0 = Sunday .. 6 = Saturday.  mon: 0 = January .. 11 = December.
struct Holiday { const char* name; uint8_t mon; HolidayRule rule; uint8_t wday; uint8_t n; };

const Holiday HOLIDAYS[] = {
  { "New Year's Day",               0, HR_FIXED, 0,  1 },
  { "Martin Luther King Jr. Day",   0, HR_NTH,   1,  3 },   // 3rd Monday in January
  { "Presidents' Day",              1, HR_NTH,   1,  3 },   // 3rd Monday in February
  { "Patriots' Day",                3, HR_NTH,   1,  3 },   // 3rd Monday in April
  { "Memorial Day",                 4, HR_LAST,  1,  0 },   // last Monday in May
  { "Juneteenth",                   5, HR_FIXED, 0, 19 },
  { "Independence Day",             6, HR_FIXED, 0,  4 },
  { "Labor Day",                    8, HR_NTH,   1,  1 },   // 1st Monday in September
  { "Indigenous Peoples' Day",      9, HR_NTH,   1,  2 },   // 2nd Monday in October
  { "Veterans Day",                10, HR_FIXED, 0, 11 },
  { "Christmas Day",               11, HR_FIXED, 0, 25 },
};
const int NUM_HOLIDAYS = sizeof(HOLIDAYS) / sizeof(HOLIDAYS[0]);

// Day of week (0 = Sunday) for a Gregorian date; m is 1..12 (Sakamoto's method).
int weekdayOf(int y, int m, int d) {
  static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
  if (m < 3) y--;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int daysInMonth(int year, int mon) {
  static const int dim[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return dim[mon] + (mon == 1 && leap ? 1 : 0);
}

int holidayDay(const Holiday& h, int year) {
  switch (h.rule) {
    case HR_FIXED:
      return h.n;
    case HR_NTH: {
      int first = weekdayOf(year, h.mon + 1, 1);
      return 1 + (h.wday - first + 7) % 7 + 7 * (h.n - 1);
    }
    case HR_LAST: {
      int last = daysInMonth(year, h.mon);
      return last - (weekdayOf(year, h.mon + 1, last) - h.wday + 7) % 7;
    }
  }
  return 0;
}

// Minutes since Sunday 00:00 local wall-clock time.
const int DAY = 24 * 60;
const int TUE_3PM = 2 * DAY + 15 * 60;
const int WED_NOON = 3 * DAY + 12 * 60;
const int WED_3PM = 3 * DAY + 15 * 60;
const int THU_NOON = 4 * DAY + 12 * 60;

}  // namespace

const char* holidayName(int year, int mon, int mday) {
  for (int i = 0; i < NUM_HOLIDAYS; i++) {
    if (HOLIDAYS[i].mon == mon && holidayDay(HOLIDAYS[i], year) == mday) return HOLIDAYS[i].name;
  }
  return nullptr;
}

WasteState wasteState(time_t now, const char** delayedBy) {
  if (delayedBy) *delayedBy = nullptr;
  struct tm lt;
  localtime_r(&now, &lt);
  int m = lt.tm_wday * DAY + lt.tm_hour * 60 + lt.tm_min;   // wall-clock, so DST needs no special case
  if (m < TUE_3PM || m >= THU_NOON) return WASTE_IDLE;

  // Is there a holiday on this week's Sunday, Monday, Tuesday or Wednesday?
  // (mktime normalizes dates that cross a month or year boundary.)
  const char* why = nullptr;
  for (int off = 0; off <= 3 && !why; off++) {
    struct tm x = lt;
    x.tm_mday += off - lt.tm_wday;
    x.tm_hour = 12; x.tm_min = 0; x.tm_sec = 0;
    x.tm_isdst = -1;
    mktime(&x);
    why = holidayName(x.tm_year + 1900, x.tm_mon, x.tm_mday);
  }

  if (why) {
    if (delayedBy) *delayedBy = why;
    return (m < WED_3PM) ? WASTE_SKIPPED : WASTE_PUT_OUT;
  }
  return (m < WED_NOON) ? WASTE_PUT_OUT : WASTE_IDLE;
}
