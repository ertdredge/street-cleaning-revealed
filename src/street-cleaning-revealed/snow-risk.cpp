#include "snow-risk.h"
#include <math.h>

// True when `t` is exactly local midnight. The feed reports a day's snow start as midnight when the
// snow was already falling as the day began, i.e. the storm continues from the previous day.
static bool isLocalMidnight(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_hour == 0 && lt.tm_min == 0 && lt.tm_sec == 0;
}

int snowEventsFrom(const SnowOutlook* days, int n, SnowEvent* out, int maxOut) {
  int count = 0;
  bool open = false;                                  // inside a run of snowy days
  for (int i = 0; i < n; i++) {
    if (!days[i].snowForecast()) { open = false; continue; }
    if (open && !isLocalMidnight(days[i].startTime)) open = false;   // began after midnight: a new storm
    if (!open) {
      if (count >= maxOut) break;
      out[count].any = true;
      out[count].inches = 0.0f;
      out[count].percent = 0;
      out[count].start = days[i].startTime;
      count++;
      open = true;
    }
    SnowEvent& e = out[count - 1];
    e.inches += days[i].amountInches();
    if (days[i].probabilityPercent > e.percent) e.percent = days[i].probabilityPercent;
    if (days[i].startTime < e.start) e.start = days[i].startTime;
    if (e.percent > 100) e.percent = 100;
  }
  return count;
}

bool snowRiskOnRightBar(int year, int month) {
  int winterYear = month >= SNOW_YEAR_START_MONTH ? year : year - 1;
  return winterYear % 2 == 0;
}

int snowRiskFrom(const SnowEvent& e, int baseRisk) {
  if (!e.any || baseRisk <= 0) return 0;
  float f = e.inches / SNOW_EMERGENCY_INCHES;
  if (!(f < 1.0f)) f = 1.0f;                       // also catches NaN
  if (f < 0.0f) f = 0.0f;
  int pct = e.percent < 0 ? 0 : (e.percent > 100 ? 100 : e.percent);
  int r = (int)lroundf(baseRisk * f * (pct / 100.0f));
  return r > 90 ? 90 : r;
}
