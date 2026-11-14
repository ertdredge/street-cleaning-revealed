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

static void testTypicalLows() {
  printf("typical lows\n");
  CHECK_NEAR(typicalLowC(1, 1), -3.9, 1e-4);
  CHECK_NEAR(typicalLowC(4, 23), 6.7, 1e-4);
  CHECK_NEAR(typicalLowC(7, 20), 19.4, 1e-4);
  CHECK_NEAR(typicalLowC(11, 12), 3.9, 1e-4);             // the night of Nov 11 is compared with this date
  CHECK_NEAR(typicalLowC(2, 29), -2.8, 1e-4);             // leap day exists
  CHECK_NEAR(typicalLowC(12, 29), -3.3, 1e-4);
  CHECK(isnan(typicalLowC(2, 30)));
  CHECK(isnan(typicalLowC(4, 31)));
  CHECK(isnan(typicalLowC(0, 1)));
  CHECK(isnan(typicalLowC(13, 1)));
  CHECK(isnan(typicalLowC(1, 0)));
  for (int m = 1; m <= 12; m++)                           // every valid date resolves, below that day's typical high
    for (int d = 1; d <= 31; d++) {
      float lo = typicalLowC(m, d), hi = typicalHighC(m, d);
      CHECK(isnan(lo) == isnan(hi));
      if (!isnan(lo)) CHECK(lo > -25.0f && lo < 25.0f && lo < hi);
    }
}

static void testRollover() {
  printf("rollover hours\n");
  CHECK(topDayOffset(0, 17) == 0 && topDayOffset(16, 17) == 0);
  CHECK(topDayOffset(17, 17) == 1 && topDayOffset(23, 17) == 1);
  CHECK(topNightOffset(0, 6) == -1 && topNightOffset(5, 6) == -1);       // until 06:00: the night in progress
  CHECK(topNightOffset(6, 6) == 0 && topNightOffset(23, 6) == 0);        // from 06:00: tonight
}

static void testBar() {
  printf("bar length\n");
  CHECK(barPixelsFor(0.0f) == 0);
  CHECK(barPixelsFor(0.49f) == 0 && barPixelsFor(-0.49f) == 0);
  CHECK(barPixelsFor(0.5f) == 1 && barPixelsFor(-0.5f) == -1);
  CHECK(barPixelsFor(0.99f) == 1 && barPixelsFor(1.0f) == 2);
  CHECK(barPixelsFor(7.0f) == 14 && barPixelsFor(-7.0f) == -14);          // an exact whole degree is not 13
  CHECK(barPixelsFor(22.8f - 15.6f) == 14);                              // the doc's example: 7.2 above typical
  CHECK(barPixelsFor(14.5f) == 29 && barPixelsFor(14.99f) == 29);
  CHECK(barPixelsFor(15.0f) == 30 && barPixelsFor(-15.0f) == -30);
  CHECK(barPixelsFor(40.0f) == 30 && barPixelsFor(-40.0f) == -30);        // capped at the half
  CHECK(barPixelsFor(INFINITY) == 30 && barPixelsFor(-INFINITY) == -30);
  CHECK(barPixelsFor(NAN) == 0);
  for (int half = 1; half <= 60; half++) {                               // every half degree, however it rounds
    float d = half * 0.5f;
    int want = half > 30 ? 30 : half;
    CHECK(barPixelsFor(d) == want && barPixelsFor(-d) == -want);
    CHECK(barPixelsFor(7.2f - 0.2f + (half - 14) * 0.5f) == (half > 30 ? 30 : half));   // accumulated float error
  }
}

static void testNotchBoundary() {
  printf("notch boundary\n");
  CHECK(notchBoundaryFor(0.0f) == 30 && notchBoundaryFor(0.4f) == 30 && notchBoundaryFor(-0.4f) == 30);
  CHECK(notchBoundaryFor(NAN) == 30);
  CHECK(notchBoundaryFor(-2.3f) == 26);                                  // the doc's example: pixels 25 and 26
  CHECK(notchBoundaryFor(2.3f) == 34);
  CHECK(notchBoundaryFor(7.0f) == 44 && notchBoundaryFor(-7.0f) == 16);
  CHECK(notchBoundaryFor(14.5f) == 59 && notchBoundaryFor(-14.5f) == 1);
  CHECK(notchBoundaryFor(15.0f) == 59 && notchBoundaryFor(-15.0f) == 1); // clamped: the end pixels
  CHECK(notchBoundaryFor(80.0f) == 59 && notchBoundaryFor(-80.0f) == 1);
  CHECK(notchBoundaryFor(INFINITY) == 59 && notchBoundaryFor(-INFINITY) == 1);
}

// Counts of pixels of each kind in an image.
static int countOf(const Rgb* img, const Rgb& c) { int n = 0; for (int i = 0; i < TOP_STRIP_PIXELS; i++) n += same(img[i], c); return n; }

static void testImage() {
  printf("images\n");
  Rgb img[TOP_STRIP_PIXELS];
  weatherTopImage(false, 0, false, 0, img);                              // no high, no low: all background
  for (int i = 0; i < TOP_STRIP_PIXELS; i++) CHECK(same(img[i], WEATHER_TYPICAL));
  weatherTopImage(true, 0.3f, false, 0, img);                            // typical high: all background
  CHECK(countOf(img, WEATHER_TYPICAL) == 60);

  weatherTopImage(true, 7.2f, false, 0, img);                            // warm: pixels 30..43 scarlet
  for (int i = 0; i < 60; i++) CHECK(same(img[i], (i >= 30 && i <= 43) ? WEATHER_SCARLET : WEATHER_TYPICAL));
  weatherTopImage(true, -7.2f, false, 0, img);                           // cold: pixels 29 down to 16
  for (int i = 0; i < 60; i++) CHECK(same(img[i], (i >= 16 && i <= 29) ? WEATHER_SKY_BLUE : WEATHER_TYPICAL));
  weatherTopImage(true, 20.0f, false, 0, img);                           // the whole warm half, nothing on the cold side
  for (int i = 0; i < 60; i++) CHECK(same(img[i], i >= 30 ? WEATHER_SCARLET : WEATHER_TYPICAL));
  weatherTopImage(true, -20.0f, false, 0, img);
  for (int i = 0; i < 60; i++) CHECK(same(img[i], i < 30 ? WEATHER_SKY_BLUE : WEATHER_TYPICAL));
  weatherTopImage(true, 0.5f, false, 0, img);                            // one pixel is a bar
  CHECK(same(img[30], WEATHER_SCARLET) && countOf(img, WEATHER_SCARLET) == 1);
  weatherTopImage(true, NAN, false, 0, img);                             // an unknown high is no bar
  CHECK(countOf(img, WEATHER_TYPICAL) == 60);

  // the notch alone
  weatherTopImage(false, 0, true, 0.0f, img);                            // at the center: pixels 29 and 30
  for (int i = 0; i < 60; i++) CHECK(dark(img[i]) == (i == 29 || i == 30));
  weatherTopImage(false, 0, true, -2.3f, img);                           // pixels 25 and 26
  for (int i = 0; i < 60; i++) CHECK(dark(img[i]) == (i == 25 || i == 26));
  weatherTopImage(false, 0, true, 15.0f, img);                           // far warm: the end pixels 58 and 59
  for (int i = 0; i < 60; i++) CHECK(dark(img[i]) == (i == 58 || i == 59));
  weatherTopImage(false, 0, true, -50.0f, img);                          // far cold: pixels 0 and 1
  for (int i = 0; i < 60; i++) CHECK(dark(img[i]) == (i == 0 || i == 1));
  weatherTopImage(true, 7.2f, true, NAN, img);                           // an unknown low is no notch
  CHECK(countOf(img, Rgb{0, 0, 0}) == 0 && countOf(img, WEATHER_SCARLET) == 14);

  // the notch over the bar: a gap inside it; at its tip; opposite it
  weatherTopImage(true, 7.2f, true, 2.3f, img);                          // bar 30..43, notch 33 and 34
  for (int i = 0; i < 60; i++) {
    if (i == 33 || i == 34) CHECK(dark(img[i]));
    else CHECK(same(img[i], (i >= 30 && i <= 43) ? WEATHER_SCARLET : WEATHER_TYPICAL));
  }
  weatherTopImage(true, 7.0f, true, 7.0f, img);                          // same deviation: bar 30..43, b = 44 -> 43 and 44 dark
  CHECK(same(img[42], WEATHER_SCARLET) && dark(img[43]) && dark(img[44]) && same(img[45], WEATHER_TYPICAL));
  CHECK(countOf(img, WEATHER_SCARLET) == 13);
  weatherTopImage(true, 7.0f, true, -7.0f, img);                         // warm day, cold night: bar right, notch left
  CHECK(countOf(img, WEATHER_SCARLET) == 14 && dark(img[15]) && dark(img[16]) && countOf(img, Rgb{0, 0, 0}) == 2);
  weatherTopImage(true, -7.0f, true, -7.0f, img);                        // cold bar 16..29, b = 16: pixels 15 and 16
  CHECK(same(img[17], WEATHER_SKY_BLUE) && dark(img[16]) && dark(img[15]) && same(img[14], WEATHER_TYPICAL));
  weatherTopImage(true, 30.0f, true, 0.0f, img);                         // a notch at the center splits nothing: 29 is background, 30 gap
  CHECK(dark(img[29]) && dark(img[30]) && same(img[31], WEATHER_SCARLET));

  // always exactly two dark pixels when there is a notch, and never more than the lit-ness rules allow
  for (int tenth = -400; tenth <= 400; tenth += 7) {
    float d = tenth / 10.0f;
    weatherTopImage(true, d * 0.37f, true, d, img);
    CHECK(countOf(img, Rgb{0, 0, 0}) == 2);
  }
}

// The background must read as brighter than black and dimmer than the bars (3.1): luminance about 75% of the bars'.
static void testColors() {
  printf("colors\n");
  double ly = 0.2126 * WEATHER_TYPICAL.r + 0.7152 * WEATHER_TYPICAL.g + 0.0722 * WEATHER_TYPICAL.b;
  double sc = 0.2126 * WEATHER_SCARLET.r + 0.7152 * WEATHER_SCARLET.g + 0.0722 * WEATHER_SCARLET.b;
  double sb = 0.2126 * WEATHER_SKY_BLUE.r + 0.7152 * WEATHER_SKY_BLUE.g + 0.0722 * WEATHER_SKY_BLUE.b;
  CHECK_NEAR(sc, sb, 1.5);
  CHECK_NEAR(ly / sc, 0.75, 0.05);
  CHECK(WEATHER_TYPICAL.g > WEATHER_TYPICAL.r && WEATHER_TYPICAL.g > WEATHER_TYPICAL.b);   // faintly green
}

int main() {
  testTypicalHighs();
  testTypicalLows();
  testRollover();
  testBar();
  testNotchBoundary();
  testImage();
  testColors();
  printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
