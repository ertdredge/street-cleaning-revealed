// What the top LED strip shows for the forecast (docs/Ambient-Display-Encoding.md, 3.1 and 3.2).
// Pure calculation: no Arduino.h and no FastLED, so it builds and is tested on a Mac or Linux host
// (`make test` in test/). The sketch turns the Rgb values into CRGB.
//
// The 60 pixels are one scale, like a number line laid on its side. "Typical for the date" is the center
// (the boundary between pixels 29 and 30); warmer is to the right and colder to the left, two pixels per
// degree C. All pixels are lit in a pale background. The forecast high is a solid bar growing from the
// center (scarlet to the right, sky blue to the left); tonight's low is a two-pixel dark notch on the same scale.
#pragma once
#include <stdint.h>

struct Rgb { uint8_t r, g, b; };

const int TOP_STRIP_PIXELS = 60;
const int TOP_HALF_PIXELS = TOP_STRIP_PIXELS / 2;   // pixels 0..29 are the cold half, 30..59 the warm half
const int TOP_PIXELS_PER_C = 2;                     // scale: one pixel per half degree C
const int NOTCH_PIXELS = 2;                         // width of the night notch

const Rgb WEATHER_TYPICAL  = {  35,  43,  38 };   // pale green-tinted off-white background, 75% of the bar luminance
const Rgb WEATHER_SCARLET  = { 173,  24,   0 };   // warmer than typical
const Rgb WEATHER_SKY_BLUE = {  38,  58,  66 };   // colder than typical

// The hour of the local day at which the top strip changes what it shows (Ambient-Display-Encoding.md 3.4.2):
// the bar, breathing and sparkle move to tomorrow's forecast from 5 pm; the notch moves to the night that
// begins today from 6 am.
//   topDayOffset(hour, tomorrowAfter)  0 = today's forecast, 1 = tomorrow's
//   topNightOffset(hour, lowAfter)     -1 = the night that began yesterday, 0 = tonight
int topDayOffset(int hour, int tomorrowAfterHour);
int topNightOffset(int hour, int lowAfterHour);

// Typical high in deg C for each day of a leap year, from docs/typical-high-temperatures.csv
// (generated into typical-highs.cpp by bin/make-typical-highs).
extern const float TYPICAL_HIGH_C[366];

// Typical high for a calendar date; month 1..12, mday 1..31. NAN for a date that does not exist
// (February 30, month 13...). February 29 is valid.
float typicalHighC(int month, int mday);

// Typical low in deg C for each day of a leap year, from docs/typical-low-temperatures.csv
// (generated into typical-lows.cpp by the same bin/make-typical-highs).
extern const float TYPICAL_LOW_C[366];

// Typical low for a calendar date, with the same arguments and NAN rule as typicalHighC(). The night's
// low is compared with the date of the morning on which the night ends (Ambient-Display-Encoding.md 3.2.3).
float typicalLowC(int month, int mday);

// Signed bar length for a forecast high `deltaC` degrees from typical: one pixel per half degree, rounded
// down (an exact half or whole degree counts despite floating-point error), capped at 30. Positive is warm
// (the bar grows rightward from pixel 30), negative is cold (leftward from pixel 29), 0 within half a degree
// of typical, and for NaN. Infinity gives the cap.
int barPixelsFor(float deltaC);

// Boundary b of the night notch for a forecast low `deltaC` degrees from the typical low: pixels b - 1 and b
// are dark. b = 30 + sign x floor(2|deltaC|), clamped to 1..59. 30 for NaN.
int notchBoundaryFor(float deltaC);

// The 60-pixel image: the background, then the bar, then the notch (black) over both. haveHigh false (no
// forecast high) gives no bar; haveLow false (no forecast low) gives no notch.
void weatherTopImage(bool haveHigh, float highDeltaC, bool haveLow, float lowDeltaC, Rgb out[TOP_STRIP_PIXELS]);
