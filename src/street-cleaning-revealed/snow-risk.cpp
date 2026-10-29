#include "snow-risk.h"
#include <math.h>

SnowEvent snowEventFrom(const SnowOutlook& today, const SnowOutlook& tomorrow) {
  SnowEvent e = { false, 0.0f, 0, 0 };
  const SnowOutlook* d[2] = { &today, &tomorrow };
  for (int i = 0; i < 2; i++) {
    if (!d[i]->snowForecast()) continue;
    e.any = true;
    e.inches += d[i]->amountInches();
    if (d[i]->probabilityPercent > e.percent) e.percent = d[i]->probabilityPercent;
    if (e.start == 0 || d[i]->startTime < e.start) e.start = d[i]->startTime;
  }
  if (e.percent > 100) e.percent = 100;
  return e;
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
