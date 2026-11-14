// See weather-display.h.
#include "weather-display.h"
#include <math.h>

namespace {

const float MAX_DELTA_C = 100.0f;      // far beyond any real deviation; keeps the arithmetic in range

const int DAYS_IN_MONTH_LEAP[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

}  // namespace

namespace {
// Position of a date in a leap-year table (0 = January 1), or -1 for a date that does not exist.
int leapYearIndex(int month, int mday) {
  if (month < 1 || month > 12 || mday < 1 || mday > DAYS_IN_MONTH_LEAP[month - 1]) return -1;
  int index = mday - 1;
  for (int m = 0; m < month - 1; m++) index += DAYS_IN_MONTH_LEAP[m];
  return index;
}
}  // namespace

float typicalHighC(int month, int mday) {
  int i = leapYearIndex(month, mday);
  return i < 0 ? NAN : TYPICAL_HIGH_C[i];
}

float typicalLowC(int month, int mday) {
  int i = leapYearIndex(month, mday);
  return i < 0 ? NAN : TYPICAL_LOW_C[i];
}

int topDayOffset(int hour, int tomorrowAfterHour) { return hour >= tomorrowAfterHour ? 1 : 0; }
int topNightOffset(int hour, int lowAfterHour) { return hour >= lowAfterHour ? 0 : -1; }

namespace {
// floor(2|d|) with the epsilon that keeps an exact half or whole degree (7.2 - 0.2 in floating point...)
// from falling just below its boundary; 0 for NaN; capped at 30.
int halfDegrees(float deltaC) {
  if (!(fabsf(deltaC) > 0.0f)) return 0;                       // zero or NaN
  float a = fabsf(deltaC);
  if (!(a < MAX_DELTA_C)) a = MAX_DELTA_C;                     // also keeps infinity out of the int conversion
  int n = (int)floorf(a * TOP_PIXELS_PER_C + 0.001f);
  return n > TOP_HALF_PIXELS ? TOP_HALF_PIXELS : n;
}
}  // namespace

int barPixelsFor(float deltaC) {
  int n = halfDegrees(deltaC);
  return deltaC < 0 ? -n : n;
}

int notchBoundaryFor(float deltaC) {
  int b = TOP_HALF_PIXELS + (deltaC < 0 ? -halfDegrees(deltaC) : halfDegrees(deltaC));
  if (b < 1) b = 1;
  if (b > TOP_STRIP_PIXELS - 1) b = TOP_STRIP_PIXELS - 1;
  return b;
}

void weatherTopImage(bool haveHigh, float highDeltaC, bool haveLow, float lowDeltaC, Rgb out[TOP_STRIP_PIXELS]) {
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) out[i] = WEATHER_TYPICAL;
  if (haveHigh) {
    int n = barPixelsFor(highDeltaC);
    for (int i = 0; i < n; i++)  out[TOP_HALF_PIXELS + i] = WEATHER_SCARLET;
    for (int i = 0; i < -n; i++) out[TOP_HALF_PIXELS - 1 - i] = WEATHER_SKY_BLUE;
  }
  if (haveLow && !isnan(lowDeltaC)) {
    int b = notchBoundaryFor(lowDeltaC);
    const Rgb dark = { 0, 0, 0 };
    for (int i = 0; i < NOTCH_PIXELS; i++) out[b - 1 + i] = dark;
  }
}
