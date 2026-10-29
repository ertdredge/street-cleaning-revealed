// See weather-display.h.
#include "weather-display.h"
#include <math.h>

namespace {

const float MAX_DELTA_C = 100.0f;      // far beyond any real deviation; keeps the arithmetic in range

const int DAYS_IN_MONTH_LEAP[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

// Linear interpolation of the three LED channels from a to b by pct percent (0..100).
Rgb blend(const Rgb& a, const Rgb& b, int pct) {
  Rgb o;
  o.r = (uint8_t)lroundf(a.r + (b.r - (float)a.r) * pct / 100.0f);
  o.g = (uint8_t)lroundf(a.g + (b.g - (float)a.g) * pct / 100.0f);
  o.b = (uint8_t)lroundf(a.b + (b.b - (float)a.b) * pct / 100.0f);
  return o;
}

}  // namespace

float typicalHighC(int month, int mday) {
  if (month < 1 || month > 12 || mday < 1 || mday > DAYS_IN_MONTH_LEAP[month - 1]) return NAN;
  int index = mday - 1;
  for (int m = 0; m < month - 1; m++) index += DAYS_IN_MONTH_LEAP[m];
  return TYPICAL_HIGH_C[index];
}

TopShape topShapeFor(float deltaC) {
  TopShape s = { 0, 0, 0, 0 };
  if (!(fabsf(deltaC) > 0.0f)) return s;                       // zero or NaN
  // The small epsilon keeps an exact half or whole degree (7.2 - 0.2 in floating point...) from
  // falling just below its boundary.
  float a = fabsf(deltaC);
  if (!(a < MAX_DELTA_C)) a = MAX_DELTA_C;                     // also keeps infinity out of the int conversion
  int half = (int)floorf(a * 2.0f + 0.001f);
  int full = (int)floorf(a * BLEED_STEPS_PER_C + 0.001f);
  if (half > TOP_GROUP_PIXELS) half = TOP_GROUP_PIXELS;       // the hot (cold) group has 20 pixels
  // `full` is not capped at 20: the bleed keeps advancing through the temperate group as the
  // deviation grows, until every pixel of it is fully shifted (see weatherTopImage).
  if (deltaC > 0) { s.hotPixels = half;  s.hotBleed = full; }
  else            { s.coldPixels = half; s.coldBleed = full; }
  return s;
}

void weatherTopImage(bool haveDelta, float deltaC, Rgb out[TOP_STRIP_PIXELS]) {
  const Rgb dark = { 0, 0, 0 };
  const int centerFirst = TOP_GROUP_PIXELS, centerLast = 2 * TOP_GROUP_PIXELS - 1;
  for (int i = 0; i < TOP_STRIP_PIXELS; i++)
    out[i] = (i >= centerFirst && i <= centerLast) ? WEATHER_TYPICAL : dark;
  if (!haveDelta) return;

  TopShape s = topShapeFor(deltaC);
  for (int i = 0; i < s.hotPixels; i++)  out[centerLast + 1 + i] = WEATHER_SCARLET;
  for (int i = 0; i < s.coldPixels; i++) out[centerFirst - 1 - i] = WEATHER_SKY_BLUE;
  // The pixel j places in from the temperate group's edge is shifted BLEED_PERCENT_PER_STEP x (bleed - j), at most 100%.
  // The shifted region therefore keeps advancing as the deviation grows, until the whole group is
  // fully shifted (at the defaults 29 degrees: the far pixel, j = 19, reaches 100%). It never leaves the temperate
  // group: scarlett does not reach the cold group and sky blue does not reach the hot group.
  for (int j = 0; j < s.hotBleed && j < TOP_GROUP_PIXELS; j++) {       // j = 0 is the right edge
    int pct = BLEED_PERCENT_PER_STEP * (s.hotBleed - j);
    out[centerLast - j] = blend(WEATHER_TYPICAL, WEATHER_SCARLET, pct > 100 ? 100 : pct);
  }
  for (int j = 0; j < s.coldBleed && j < TOP_GROUP_PIXELS; j++) {      // j = 0 is the left edge
    int pct = BLEED_PERCENT_PER_STEP * (s.coldBleed - j);
    out[centerFirst + j] = blend(WEATHER_TYPICAL, WEATHER_SKY_BLUE, pct > 100 ? 100 : pct);
  }
}
