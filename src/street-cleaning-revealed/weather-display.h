// What the top LED strip shows for a day's forecast high (docs/Ambient-Display-Encoding.md, 3.1 and 3.2).
// Pure calculation: no Arduino.h and no FastLED, so it builds and is tested on a Mac or Linux host
// (`make test` in test/). The sketch turns the Rgb values into CRGB.
//
// The strip is three groups of 20 pixels, left to right: cold, temperate, hot. The temperate group is
// spring green. A forecast high above the typical high for the date grows scarlett into the hot group
// and bleeds it into the temperate group; a high below typical does the same, mirrored, with sky blue.
#pragma once
#include <stdint.h>

struct Rgb { uint8_t r, g, b; };

const int TOP_GROUP_PIXELS = 20;
const int TOP_STRIP_PIXELS = 3 * TOP_GROUP_PIXELS;

const Rgb WEATHER_TYPICAL  = {   0,  72,  36 };   // spring green: the temperate group
const Rgb WEATHER_SCARLET  = { 173,  24,   0 };   // warmer than typical
const Rgb WEATHER_SKY_BLUE = {  38,  58,  66 };   // colder than typical

// Bleed tuning (3.2): how fast the shift advances through the temperate group.
//   BLEED_STEPS_PER_C       bleed steps (pixels of advance) per degree C of deviation
//   BLEED_PERCENT_PER_STEP  percent shift each step adds; the width of the soft edge is 100 / this
// The whole group is fully shifted at about 19 / BLEED_STEPS_PER_C + 100 / BLEED_PERCENT_PER_STEP degrees.
const float BLEED_STEPS_PER_C = 1.0f;
const int BLEED_PERCENT_PER_STEP = 25;

// Typical high in deg C for each day of a leap year, from docs/typical-high-temperatures.csv
// (generated into typical-highs.cpp by bin/make-typical-highs).
extern const float TYPICAL_HIGH_C[366];

// Typical high for a calendar date; month 1..12, mday 1..31. NAN for a date that does not exist
// (February 30, month 13...). February 29 is valid.
float typicalHighC(int month, int mday);

// How far a high that is `deltaC` degrees from typical shows on the strip.
//   hotPixels   scarlett pixels in the hot group: one per half degree above typical, from the
//               group's left (center-facing) edge, at most 20
//   hotBleed    how far scarlett has bled into the temperate group: one per full degree above typical,
//               not capped. The pixel j places in from the group's right edge is shifted toward scarlett
//               by BLEED_PERCENT_PER_STEP x (hotBleed - j), at most 100%, so the shifted region keeps advancing leftward as
//               the high climbs and the whole group is fully scarlett from 19 / BLEED_STEPS_PER_C + 100 / BLEED_PERCENT_PER_STEP degrees above typical (29 by default).
//               It never crosses into the cold group.
//   coldPixels, coldBleed   the same for a high below typical, mirrored (sky blue grows leftward
//               from the cold group's right edge and bleeds rightward from the temperate group's left
//               edge, never into the hot group)
// Only one side is ever nonzero. Less than half a degree from typical shows nothing: the default display.
struct TopShape { int hotPixels, hotBleed, coldPixels, coldBleed; };
TopShape topShapeFor(float deltaC);

// The 60-pixel image. With haveDelta false (no forecast high) it is the default display of 3.1: the
// temperate group lit and the others dark.
void weatherTopImage(bool haveDelta, float deltaC, Rgb out[TOP_STRIP_PIXELS]);
