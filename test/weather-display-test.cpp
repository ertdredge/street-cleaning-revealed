// Unit tests for weather-display.h/.cpp (the top strip's temperature display): `make test` here.
// Each CHECK prints its line number on failure; the exit status is nonzero if any failed.
#include "weather-display.h"
#include <math.h>
#include <stdio.h>

static int checks = 0, failures = 0;
#define CHECK(cond) do { checks++; if (!(cond)) { failures++; printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { checks++; double a_ = (a), b_ = (b); \
  if (!(fabs(a_ - b_) <= (tol))) { failures++; printf("  FAIL line %d: %s = %g, expected %g\n", __LINE__, #a, a_, b_); } } while (0)

static bool same(const Rgb& a, const Rgb& b) { return a.r == b.r && a.g == b.g && a.b == b.b; }
static bool dark(const Rgb& a) { return a.r == 0 && a.g == 0 && a.b == 0; }
static bool nearRgb(const Rgb& a, const Rgb& b) { return abs(a.r - b.r) <= 1 && abs(a.g - b.g) <= 1 && abs(a.b - b.b) <= 1; }
// What a pixel pct percent of the way from the temperate green to `target` should be, worked out here
// independently of the implementation.
static Rgb expected(const Rgb& target, int pct) {
  double f = pct / 100.0;
  Rgb o;
  o.r = (uint8_t)floor(WEATHER_TYPICAL.r + (target.r - WEATHER_TYPICAL.r) * f + 0.5);
  o.g = (uint8_t)floor(WEATHER_TYPICAL.g + (target.g - WEATHER_TYPICAL.g) * f + 0.5);
  o.b = (uint8_t)floor(WEATHER_TYPICAL.b + (target.b - WEATHER_TYPICAL.b) * f + 0.5);
  return o;
}

// The exact-value tests below describe the default bleed tuning; with other values they are skipped
// (the properties that hold for any tuning are still checked).
static const bool DEFAULT_TUNING = BLEED_STEPS_PER_C == 1.0f && BLEED_PERCENT_PER_STEP == 10;

static void testTypicalHighs() {
  printf("typical highs\n");
  CHECK_NEAR(typicalHighC(1, 1), 3.3, 1e-4);
  CHECK_NEAR(typicalHighC(4, 23), 15.6, 1e-4);            // the example in the encoding doc
  CHECK_NEAR(typicalHighC(7, 20), 28.3, 1e-4);
  CHECK_NEAR(typicalHighC(2, 28), 5.6, 1e-4);
  CHECK_NEAR(typicalHighC(2, 29), 5.6, 1e-4);             // leap day exists
  CHECK_NEAR(typicalHighC(3, 1), 5.6, 1e-4);
  CHECK(!isnan(typicalHighC(12, 31)));
  CHECK(isnan(typicalHighC(2, 30)));
  CHECK(isnan(typicalHighC(4, 31)));
  CHECK(isnan(typicalHighC(0, 1)));
  CHECK(isnan(typicalHighC(13, 1)));
  CHECK(isnan(typicalHighC(1, 0)));
  for (int m = 1; m <= 12; m++)                           // every valid date resolves, to a plausible value
    for (int d = 1; d <= 31; d++) {
      float t = typicalHighC(m, d);
      if (!isnan(t)) CHECK(t > 0.0f && t < 35.0f);
    }
}

static void testShape() {
  printf("shape\n");
  TopShape s = topShapeFor(0.0f);
  CHECK(s.hotPixels == 0 && s.hotBleed == 0 && s.coldPixels == 0 && s.coldBleed == 0);
  s = topShapeFor(NAN);
  CHECK(s.hotPixels == 0 && s.coldPixels == 0);
  s = topShapeFor(0.4f);                                  // under half a degree: nothing
  CHECK(s.hotPixels == 0 && s.hotBleed == 0);
  s = topShapeFor(0.5f);
  CHECK(s.hotPixels == 1 && s.hotBleed == 0);
  s = topShapeFor(0.99f);
  CHECK(s.hotPixels == 1 && s.hotBleed == 0);
  if (!DEFAULT_TUNING) return;                            // the rest assumes the default bleed tuning
  s = topShapeFor(1.0f);
  CHECK(s.hotPixels == 2 && s.hotBleed == 1);
  s = topShapeFor(22.8f - 15.6f);                         // the doc's example: 7.2 above typical
  CHECK(s.hotPixels == 14 && s.hotBleed == 7 && s.coldPixels == 0 && s.coldBleed == 0);
  s = topShapeFor(15.6f - 22.8f);                         // mirrored
  CHECK(s.coldPixels == 14 && s.coldBleed == 7 && s.hotPixels == 0 && s.hotBleed == 0);
  s = topShapeFor(2.0f);                                  // whole degrees must not fall just under
  CHECK(s.hotPixels == 4 && s.hotBleed == 2);
  s = topShapeFor((float)(13.3333 - 11.3333));
  CHECK(s.hotPixels == 4 && s.hotBleed == 2);
  s = topShapeFor(10.0f);
  CHECK(s.hotPixels == 20 && s.hotBleed == 10);
  s = topShapeFor(25.0f);                                 // the pixel count is capped at a group; the bleed is not
  CHECK(s.hotPixels == 20 && s.hotBleed == 25);
  s = topShapeFor(-25.0f);
  CHECK(s.coldPixels == 20 && s.coldBleed == 25);
  s = topShapeFor(1e30f);                                 // absurd values stay in range
  CHECK(s.hotPixels == 20 && s.hotBleed > 25 && s.hotBleed <= 1000);
  s = topShapeFor(INFINITY);
  CHECK(s.hotPixels == 20 && s.hotBleed > 25);
}

static void testDefaultImage() {
  printf("default display\n");
  Rgb img[TOP_STRIP_PIXELS];
  weatherTopImage(false, 99.0f, img);                     // no forecast high: the delta is ignored
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) {
    if (i >= 20 && i < 40) CHECK(same(img[i], WEATHER_TYPICAL));
    else CHECK(dark(img[i]));
  }
  weatherTopImage(true, 0.0f, img);                       // exactly typical: the same display
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) {
    if (i >= 20 && i < 40) CHECK(same(img[i], WEATHER_TYPICAL));
    else CHECK(dark(img[i]));
  }
  weatherTopImage(true, 0.3f, img);                       // under half a degree
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) CHECK(same(img[i], (i >= 20 && i < 40) ? WEATHER_TYPICAL : Rgb{0, 0, 0}));
}

static void testWarm() {
  if (!DEFAULT_TUNING) { printf("testWarm skipped: non-default bleed tuning\n"); return; }
  printf("warm day\n");
  Rgb img[TOP_STRIP_PIXELS];
  weatherTopImage(true, 7.2f, img);                       // 14 scarlett pixels; 7 bleed pixels
  for (int i = 0; i < 20; i++) CHECK(dark(img[i]));       // the cold group stays dark
  for (int i = 20; i < 33; i++) CHECK(same(img[i], WEATHER_TYPICAL));   // untouched green pixels
  for (int j = 0; j < 7; j++)                             // pixel 39 is 70%, 38 is 60%, ... 33 is 10%
    CHECK(nearRgb(img[39 - j], expected(WEATHER_SCARLET, 10 * (7 - j))));
  for (int i = 40; i < 54; i++) CHECK(same(img[i], WEATHER_SCARLET));   // the 14 leftmost hot pixels
  for (int i = 54; i < 60; i++) CHECK(dark(img[i]));
  for (int i = 34; i <= 39; i++) CHECK(img[i].r > img[i - 1].r);        // more scarlett toward the right edge
}

static void testCold() {
  if (!DEFAULT_TUNING) { printf("testCold skipped: non-default bleed tuning\n"); return; }
  printf("cold day\n");
  Rgb img[TOP_STRIP_PIXELS], warm[TOP_STRIP_PIXELS];
  weatherTopImage(true, -7.2f, img);
  for (int i = 0; i < 6; i++) CHECK(dark(img[i]));        // 14 sky blue pixels in 6..19, growing leftward
  for (int i = 6; i < 20; i++) CHECK(same(img[i], WEATHER_SKY_BLUE));
  for (int i = 40; i < 60; i++) CHECK(dark(img[i]));      // the hot group stays dark
  for (int i = 27; i < 40; i++) CHECK(same(img[i], WEATHER_TYPICAL));
  for (int j = 0; j < 7; j++)                             // pixel 20 is 70%, 21 is 60%, ... 26 is 10%
    CHECK(nearRgb(img[20 + j], expected(WEATHER_SKY_BLUE, 10 * (7 - j))));
  // exact mirror: the same positions and percentages, reflected about the strip's center
  weatherTopImage(true, 7.2f, warm);
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) {
    bool warmLit = !dark(warm[i]) && !same(warm[i], WEATHER_TYPICAL);
    bool coldLit = !dark(img[TOP_STRIP_PIXELS - 1 - i]) && !same(img[TOP_STRIP_PIXELS - 1 - i], WEATHER_TYPICAL);
    CHECK(warmLit == coldLit);
  }
}

static void testBleedKeepsAdvancing() {
  printf("bleed keeps advancing\n");
  Rgb img[TOP_STRIP_PIXELS];
  if (DEFAULT_TUNING) {
  weatherTopImage(true, 10.0f, img);                      // 10 degrees: 100% at the edge, 10% ten pixels in
  CHECK(same(img[39], WEATHER_SCARLET));
  CHECK(nearRgb(img[30], expected(WEATHER_SCARLET, 10)));
  CHECK(same(img[29], WEATHER_TYPICAL));
  weatherTopImage(true, 20.0f, img);                      // 20 degrees: every pixel of the group is shifted
  for (int i = 20; i < 40; i++) CHECK(!same(img[i], WEATHER_TYPICAL));
  for (int j = 0; j < 20; j++) {
    int pct = 10 * (20 - j); if (pct > 100) pct = 100;
    CHECK(nearRgb(img[39 - j], expected(WEATHER_SCARLET, pct)));
  }
  CHECK(same(img[29], WEATHER_SCARLET));                  // pixels 29..39 are fully scarlett
  weatherTopImage(true, 28.9f, img);                      // not yet: the far pixel is at 90%
  CHECK(nearRgb(img[20], expected(WEATHER_SCARLET, 90)));
  weatherTopImage(true, 29.0f, img);                      // from 29 degrees the whole group is fully scarlett
  for (int i = 20; i < 40; i++) CHECK(same(img[i], WEATHER_SCARLET));
  weatherTopImage(true, 60.0f, img);
  for (int i = 20; i < 40; i++) CHECK(same(img[i], WEATHER_SCARLET));
  weatherTopImage(true, -29.0f, img);                     // and the same with sky blue
  for (int i = 20; i < 40; i++) CHECK(same(img[i], WEATHER_SKY_BLUE));
  }
  // as the deviation grows the shift never decreases anywhere, so the display only moves one way
  for (int sign = -1; sign <= 1; sign += 2) {
    Rgb prev[TOP_STRIP_PIXELS];
    weatherTopImage(true, sign * 0.0f, prev);
    for (float d = 0.1f; d <= 40.0f; d += 0.1f) {
      weatherTopImage(true, sign * d, img);
      const Rgb& target = sign > 0 ? WEATHER_SCARLET : WEATHER_SKY_BLUE;
      for (int i = 20; i < 40; i++) {
        int before = abs(prev[i].g - target.g) + abs(prev[i].r - target.r) + abs(prev[i].b - target.b);
        int after = abs(img[i].g - target.g) + abs(img[i].r - target.r) + abs(img[i].b - target.b);
        CHECK(after <= before);
      }
      for (int i = 0; i < TOP_STRIP_PIXELS; i++) prev[i] = img[i];
    }
  }
}

static void testNeverCrossesBlocks() {
  printf("bleed stays in the temperate group\n");
  Rgb img[TOP_STRIP_PIXELS];
  for (float d = 0.0f; d <= 100.0f; d += 0.5f) {
    weatherTopImage(true, d, img);                        // warm: nothing but scarlett in the hot group, cold group dark
    for (int i = 0; i < 20; i++) CHECK(dark(img[i]));
    for (int i = 40; i < 60; i++) CHECK(dark(img[i]) || same(img[i], WEATHER_SCARLET));
    weatherTopImage(true, -d, img);                       // cold: the hot group dark, nothing but sky blue in the cold group
    for (int i = 40; i < 60; i++) CHECK(dark(img[i]));
    for (int i = 0; i < 20; i++) CHECK(dark(img[i]) || same(img[i], WEATHER_SKY_BLUE));
  }
  weatherTopImage(true, 1e30f, img);
  for (int i = 0; i < 20; i++) CHECK(dark(img[i]));
  for (int i = 20; i < 40; i++) CHECK(same(img[i], WEATHER_SCARLET));
  for (int i = 40; i < 60; i++) CHECK(same(img[i], WEATHER_SCARLET));
}

int main() {
  testTypicalHighs();
  testShape();
  testDefaultImage();
  testWarm();
  testCold();
  testBleedKeepsAdvancing();
  testNeverCrossesBlocks();
  printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
