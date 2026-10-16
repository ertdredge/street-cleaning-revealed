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

// Day of month of the n-th `wday` (0 = Sunday) of the month; mon is 0..11.
int nthWeekdayDay(int year, int mon, int wday, int n) {
  int first = weekdayOf(year, mon + 1, 1);
  return 1 + (wday - first + 7) % 7 + 7 * (n - 1);
}

// Days since 1970-01-01 for a Gregorian date; mon is 0..11 (Hinnant's days_from_civil).
long dayNumber(int y, int mon, int d) {
  int m = mon + 1;
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

int holidayDay(const Holiday& h, int year) {
  switch (h.rule) {
    case HR_FIXED:
      return h.n;
    case HR_NTH:
      return nthWeekdayDay(year, h.mon, h.wday, h.n);
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

bool yardWasteWeek(int year, int mon, int sundayMday) {
  long s = dayNumber(year, mon, sundayMday);
  long a = dayNumber(year, 3, nthWeekdayDay(year, 3, 0, 2));     // 2nd Sunday in April
  long m = dayNumber(year, 4, nthWeekdayDay(year, 4, 6, 2));     // 2nd Saturday in May
  long o = dayNumber(year, 9, nthWeekdayDay(year, 9, 0, 2));     // 2nd Sunday in October
  long d = dayNumber(year, 11, nthWeekdayDay(year, 11, 0, 2));   // 2nd Sunday in December
  if (s < a) return false;
  if (s <= m) return true;                       // m is a Saturday, so this is every week ending by m
  if (s < o) return ((s - (m + 1)) / 7) % 2 == 1; // first week after m is trash only, then alternate
  if (s < d) return true;
  return false;
}

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

  struct tm sun = lt;                      // this week's Sunday
  sun.tm_mday -= lt.tm_wday;
  sun.tm_hour = 12; sun.tm_min = 0; sun.tm_sec = 0;
  sun.tm_isdst = -1;
  mktime(&sun);
  const WasteState putOut = yardWasteWeek(sun.tm_year + 1900, sun.tm_mon, sun.tm_mday)
                                ? WASTE_PUT_OUT_YARD : WASTE_PUT_OUT;

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
    return (m < WED_3PM) ? WASTE_SKIPPED : putOut;
  }
  return (m < WED_NOON) ? putOut : WASTE_IDLE;
}
