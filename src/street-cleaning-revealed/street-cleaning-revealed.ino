// Street Cleaning ReveaLED: ambient display driver for two 150-pixel WS2815 runs.
// Serial test harness: 115200 baud, newline-terminated commands.
//
// Hardware:
//   Run A: GPIO 27 -> 74AHCT125N gate 1 -> 470 ohm -> left strip (90) -> top strip (60)
//   Run B: GPIO 33 -> 74AHCT125N gate 2 -> 470 ohm -> bottom strip (60) -> right strip (90)
// One 300-element array: indexes 0..149 are run A, 150..299 are run B.
// In chain order every strip already runs in its natural display direction:
// left/right bottom -> top, top/bottom left -> right.
//
// Architecture (encoding rules: docs/Ambient-Display-Encoding.md):
//   Layer 0  geometry    Strip table; the only code that knows about the daisy chain
//   Layer 1  primitives  px(), fillStrip()
//   Layer 2  setters     setLeft/setRight/setTop/setBottom; they set state, no LED I/O
//   Layer 3  renderer    renderFrame(), called continuously from loop(); non-blocking
//   Weather  a FreeRTOS task on core 0 polls the NWS forecast hourly (weather-feed, weather-forecast);
//            the display only reads the latest snapshot, so the fetch never stalls the renderer
//
// Commands (manual commands use the current color and switch clock mode off):
//   left N         light N pixels (0..90) from the bottom of the left strip
//   right N        light N pixels (0..90) from the bottom of the right strip
//   precip P       force the top strip's chance of precipitation to P (0..100), then render;
//                  `precip off` goes back to the NWS forecast. (The top strip shows the weather.)
//   high C         force the top strip's forecast high to C degrees Celsius, then render;
//                  `high off` goes back to the NWS forecast.
//   snow P IN H    pretend the forecast calls for snow: P percent chance, IN inches, accumulation
//                  starting H hours from now (negative: already falling). Drives the snow removal
//                  risk on one bar (see 1.2.1), the sparkles on that bar and the bottom strip, and the top sparkle. `snow off` goes back
//                  to the NWS forecast; `snow` alone prints the current snow event and risk.
//   emergency      print the city snow emergency: what the city feed says, any override, the phase
//                  (none, declared, in effect) and the last poll of the feed.
//   emergency start WHEN   force a snow emergency starting at WHEN (no end announced yet)
//   emergency stop WHEN    force the emergency (the forced one, else the city's) to end at WHEN
//   emergency off          drop the override and go back to the city feed
//                  WHEN is `now`, +H or -H (hours from now, fractions allowed), or YYYY-MM-DD HH:MM
//                  (US Eastern). While an emergency is declared or in effect it counts as a 100% chance
//                  of 4 inches starting at its start time, so the snow bar goes to 90 when it starts;
//                  after its end it is ignored. The override is not saved across a reboot.
//   forecast       print the weather snapshot: age, and today's and tomorrow's high, precipitation and snow
//   bottom S       bottom strip pattern, same scale
//   color r|g|b|w|y|c|m   set the current color (default w)
//   bright B       set brightness, clamped to 8..48
//   off            all dark
//   time [YYYY-MM-DD HH:MM[:SS]]   set the clock (US Eastern local time), then render the
//                  display from it; with no argument, render from the current clock.
//                  Clock mode then re-renders once a minute.
//   time START END   (two times, each YYYY-MM-DD HH:MM[:SS]) step the clock from START to END
//                  one hour at a time, one step per second. After each step it renders the
//                  LEDs and prints the usual `time` report. Any other command stops the sweep.
//   test-suite     run the built-in list of test dates in order, one every 3 seconds, printing
//                  each date and label, rendering the LEDs and printing the usual `time` report.
//                  Any other command stops it.
//   risk H         print risk and color for H hours remaining (no rendering)
//   status         print current settings
//
// Protan note: r/g are easy to confuse by eye. Prefer positions and counts.

#include <FastLED.h>
#include <cmath>
#include <sys/time.h>
#include <time.h>
#include <WiFi.h>
#include <Preferences.h>
#include "secrets.h"            // const char* WIFI_SSID, WIFI_PASS; add to .gitignore
#include "waste.h"               // holidays and the trash-day state (pure calendar logic)
#include "weather-forecast.h"   // NWS forecast parser and per-day values (pure; needs the ArduinoJson library)
#include "weather-feed.h"       // HTTPS fetch of the NWS feed (ESP32 only)
#include "weather-display.h"    // top strip image from the forecast high vs the typical high (pure)
#include "snow-risk.h"          // snow removal parking risk from the NWS forecast (pure)
#include "snow-emergency.h"     // City of Somerville snow emergencies, from the city news RSS feed (pure)
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#define DATA_PIN_A 27       // run A: left (90) -> top (60)
#define DATA_PIN_B 33       // run B: bottom (60) -> right (90)
#define RUN_LEN 150
#define NUM_LEDS (2 * RUN_LEN)
#define COLOR_ORDER GRB     // verified in A1
#define MIN_BRIGHT 8
#define MAX_BRIGHT 48       // bench cap: all 300 pixels white at 64 would be ~1.9 A, too close to the 2 A adapter

// ---- Tunables ---------------------------------------------------------------
const uint32_t FRAME_MS = 10;            // render period (100 fps; faster frames make the temporal dithering finer)
const float ANIM_PERIOD = 6.0f;          // marching pattern repeats every N pixels
const float ANIM_LIT = 2.0f;             // ...of which this many are lit
const uint8_t ANIM_DIM_LEVEL = 96;       // brightness (0..255) of the other pixels; 0 = off
const float ANIM_MAX_PPS = 20.0f;        // pixels per second at speed +-100
const int MARCH_THRESHOLD = 55;          // risk above this animates top and bottom
const int MARCH_MIN_SPEED = 10;          // speed at risk = MARCH_THRESHOLD
const int MARCH_MAX_SPEED = 100;         // speed at risk = 90

// Top strip: the weather forecast (docs/Ambient-Display-Encoding.md section 3).
const int TOP_PIXELS = TOP_STRIP_PIXELS;   // three groups of 20 (cold, temperate, hot); must equal STRIP_TOP.len
const float BREATH_MAX_HZ = 0.5f;        // breathing rate at 100% chance of precipitation (2 s period)
const uint8_t BREATH_MIN_LEVEL = 38;     // dimmest point of a breath, out of 255 (about 15% of the PWM level)
const float SPARKLE_PER_SEC = 8.0f;      // snow sparkle: new flashes per second, per strip
const float SPARKLE_SECONDS = 0.4f;      // ...each fading from white over this long
const uint8_t SPARKLE_LEVEL = 54;        // the white of a flash: constant-luminance white (54,54,54)
const float BREATH_GAMMA = 2.2f;         // the breath is a cosine in perceived brightness; its PWM level is that, to this power
const int PRECIP_MIN_PERCENT = 10;       // below this the forecast counts as dry (NWS uses 2% for "not expected"): no breathing
const int TOMORROW_AFTER_HOUR = 17;      // from 5 pm the top strip shows tomorrow's forecast
const uint32_t WEATHER_POLL_MS = 3600000UL;    // refresh the forecast hourly
const uint32_t WEATHER_RETRY_MS = 300000UL;    // after a failed fetch, try again in 5 minutes
const char* const NWS_URL = "https://api.weather.gov/gridpoints/BOX/70,102";   // Somerville (grid cell fixed at design time)
const char* const TZ_EASTERN = "EST5EDT,M3.2.0,M11.1.0";
const time_t CLOCK_VALID_AFTER = 1767225600;   // 2026-01-01 UTC; earlier means the clock is unset

// ---- Layer 0: geometry --------------------------------------------------------
struct Strip { uint16_t first; uint16_t len; };
const Strip STRIP_LEFT   = {  0, 90 };   // run A, bottom -> top
const Strip STRIP_TOP    = { 90, 60 };   // run A, left -> right
const Strip STRIP_BOTTOM = {150, 60 };   // run B, left -> right
const Strip STRIP_RIGHT  = {210, 90 };   // run B, bottom -> top

// Drawing happens in `canvas` at full scale; ditherOut() applies the global brightness and the per-pixel
// breath gain at higher precision and writes the 8-bit result into `leds`, which FastLED sends out.
CRGB canvas[NUM_LEDS];
CRGB leds[NUM_LEDS];
float sparkleOf[NUM_LEDS];               // snow sparkle: 1 = a white flash just started, fades to 0
float gainOf[NUM_LEDS];                  // per-pixel multiplier 0..1 (the top strip's breath), reset every frame
float residual[NUM_LEDS][3];             // dithering: the fraction of an output step not yet sent, per channel
uint8_t bright = 32;                     // global brightness 0..255, applied in ditherOut (FastLED's own is left at 255)

// Types used in function signatures. The Arduino build auto-generates prototypes and
// places them before the first function, so these must be defined above it.
// ---- BEGIN TYPES ----
struct Schedule { int wday; int nthA; int nthB; };   // wday: 0 = Sunday .. 6 = Saturday
struct Bar   { int n; CRGB color; };
struct AntColors { CRGB lit; CRGB other; };   // marching pattern: "lit" ants on an "other" background
struct March { AntColors colors; int speed; float phase; };
struct TopState { CRGB base[TOP_PIXELS]; int breathPct; float phase; };   // base image, chance of precipitation, breath phase 0..1
// ---- END TYPES ----

// ---- Layer 1: primitives ------------------------------------------------------
// Logical position 0 is the bottom (left/right strips) or the left end (top/bottom).
CRGB& px(const Strip& s, int pos) {
  return canvas[s.first + constrain(pos, 0, (int)s.len - 1)];
}

void fillStrip(const Strip& s, CRGB c) {
  for (int i = 0; i < s.len; i++) canvas[s.first + i] = c;
}

// ---- BEGIN PURE (no hardware; checked on the host) -----------------------------
// Risk 1..90 from hours remaining until the END of street cleaning.
// The hours are clamped to 0..504: the sextic term is NOT small for very negative t,
// so the formula must not see remaining > 504.
int riskFromHours(float remaining) {
  float t = 504.0f - constrain(remaining, 0.0f, 504.0f);   // hours elapsed in the 21-day window
  float u = t / 485.0f, u2 = u * u;
  float p = 1.0f + t / 24.0f + 69.0f * u2 * u2 * u2;
  return constrain((int)floorf(p), 1, 90);
}

// Constant-luminance green (0) -> yellow (45) -> red (90). Interpolated in linear
// light, then rescaled so every color has the luminance of full red.
const float WR = 0.2126f, WG = 0.7152f;   // relative luminance of the R and G LEDs (Rec. 709 placeholder)
const float YG = 0.55f;                   // green share of "yellow" (R = 1, G = YG)

CRGB riskColor(float x) {
  x = constrain(x, 0.0f, 90.0f);
  float r, g;
  if (x <= 45.0f) { float f = x / 45.0f;          r = f;    g = 1.0f + (YG - 1.0f) * f; }
  else            { float f = (x - 45.0f) / 45.0f; r = 1.0f; g = YG * (1.0f - f); }
  float k = WR / (WR * r + WG * g);
  return CRGB((uint8_t)lroundf(255.0f * r * k), (uint8_t)lroundf(255.0f * g * k), 0);
}

// Street cleaning ends at noon local time.
const Schedule LEFT_SCHEDULE = { 5, 2, 4 };   // 2nd and 4th Fridays
const Schedule RIGHT_SCHEDULE  = { 4, 1, 3 };   // 1st and 3rd Thursdays

// No street cleaning in January, February or March (tm_mon 0..2). The longest gap this
// creates is about 111 days (a mid-December cleaning to the first one in April).
const int FIRST_SEASON_TM_MON = 3;     // April
const int SEARCH_DAYS = 140;

// First cleaning end (noon) strictly after `now`, or 0 if none within SEARCH_DAYS.
time_t nextCleaningEnd(const Schedule& s, time_t now) {
  struct tm base;
  localtime_r(&now, &base);
  for (int d = 0; d <= SEARCH_DAYS; d++) {
    struct tm t = base;
    t.tm_mday += d;
    t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
    t.tm_isdst = -1;
    time_t end = mktime(&t);          // normalizes mday and sets wday
    if (end <= now) continue;
    if (t.tm_mon < FIRST_SEASON_TM_MON) continue;    // off season
    int nth = (t.tm_mday - 1) / 7 + 1;
    if (t.tm_wday == s.wday && (nth == s.nthA || nth == s.nthB)) return end;
  }
  return 0;
}

// ---- BEGIN WEATHER (pure) ------------------------------------------------------
// Chances of precipitation below PRECIP_MIN_PERCENT are treated as 0: a static, dry display.
int effectivePrecip(int precipPercent) {
  return precipPercent < PRECIP_MIN_PERCENT ? 0 : constrain(precipPercent, 0, 100);
}

// 3.3 breathing rate in cycles per second: proportional to the chance of precipitation.
float breathRateHz(int precipPercent) {
  return BREATH_MAX_HZ * constrain(precipPercent, 0, 100) / 100.0f;
}

// PWM gain 0..1 of a breath at `phase` (0..1 is one cycle): full at 0, BREATH_MIN_LEVEL/255 at 0.5.
// Perceived brightness follows the cosine, so the PWM gain is that raised to BREATH_GAMMA: equal
// steps look equal, instead of the dim end changing visibly in each step.
float breathGain(float phase) {
  float w = 0.5f + 0.5f * cosf(2.0f * 3.14159265f * phase);   // 1 -> 0 -> 1
  float pmin = powf(BREATH_MIN_LEVEL / 255.0f, 1.0f / BREATH_GAMMA);
  return powf(pmin + (1.0f - pmin) * w, BREATH_GAMMA);
}
// ---- END WEATHER ---------------------------------------------------------------
// ---- END PURE ------------------------------------------------------------------

// ---- Layer 2: setters (state only) --------------------------------------------

Bar leftBar = {0, CRGB::Black}, rightBar = {0, CRGB::Black};
March bottomMarch = {{CRGB::Black, CRGB::Black}, 0, 0.0f};
TopState topState = { {}, 0, 0.0f };

void setLeft(int n, CRGB c)  { leftBar  = { constrain(n, 0, (int)STRIP_LEFT.len),  c }; }
void setRight(int n, CRGB c) { rightBar = { constrain(n, 0, (int)STRIP_RIGHT.len), c }; }
// One color: the other pixels are the same color, dimmed to ANIM_DIM_LEVEL.
AntColors antsOf(CRGB c) { CRGB dim = c; dim.nscale8_video(ANIM_DIM_LEVEL); return { c, dim }; }

void setBottom(const AntColors& a, int speed) { bottomMarch.colors = a; bottomMarch.speed = constrain(speed, -100, 100); }
// Top strip: a TOP_PIXELS base image (see weatherTopImage) that breathes at breathRateHz(precipPercent).
void setTop(const CRGB* base, int precipPercent) {
  for (int i = 0; i < TOP_PIXELS; i++) topState.base[i] = base[i];   // not memcpy: FastLED 3.10 adds an fl::memcpy that makes the call ambiguous for CRGB
  topState.breathPct = constrain(precipPercent, 0, 100);
}
void setTopDark() {
  for (int i = 0; i < TOP_PIXELS; i++) topState.base[i] = CRGB::Black;
  topState.breathPct = 0;
}
void setBottom(CRGB c, int speed) { setBottom(antsOf(c), speed); }

// ---- Layer 3: renderer --------------------------------------------------------
uint32_t lastFrameMs = 0;

void drawBar(const Strip& s, const Bar& b) {
  fillStrip(s, CRGB::Black);
  for (int i = 0; i < b.n; i++) px(s, i) = b.color;
}

void advance(March& m, float dt) {
  m.phase = fmodf(m.phase + m.speed / 100.0f * ANIM_MAX_PPS * dt, ANIM_PERIOD);
  if (m.phase < 0) m.phase += ANIM_PERIOD;
}

void drawMarch(const Strip& s, const March& m) {
  for (int i = 0; i < s.len; i++) {
    float pos = fmodf(i - m.phase, ANIM_PERIOD);
    if (pos < 0) pos += ANIM_PERIOD;
    px(s, i) = (pos < ANIM_LIT) ? m.colors.lit : m.colors.other;
  }
}

void drawTop(const Strip& s, const TopState& t) {
  float gain = t.breathPct > 0 ? breathGain(t.phase) : 1.0f;
  for (int i = 0; i < s.len; i++) {
    px(s, i) = t.base[i];
    gainOf[s.first + i] = gain;          // applied at full precision in ditherOut
  }
}

// canvas x gain x brightness -> leds, in floating point, with error-feedback temporal dithering:
// each channel's rounding error is carried to the next frame, so a level between two 8-bit output
// steps is shown as a mix of the two. At 100 frames per second the mix reads as a smooth level.
void ditherOut() {
  const float scale = bright / 255.0f;
  for (int i = 0; i < NUM_LEDS; i++) {
    const uint8_t in[3] = { canvas[i].r, canvas[i].g, canvas[i].b };
    uint8_t out[3];
    for (int c = 0; c < 3; c++) {
      float sp = sparkleOf[i];
      float v = (in[c] * gainOf[i] * (1.0f - sp) + SPARKLE_LEVEL * sp) * scale;
      if (v <= 0.0f) { residual[i][c] = 0.0f; out[c] = 0; continue; }
      float t = v + residual[i][c];
      int o = (int)t;                      // t >= 0
      if (o > 255) o = 255;
      residual[i][c] = t - o;
      if (residual[i][c] > 1.0f) residual[i][c] = 1.0f;
      out[c] = (uint8_t)o;
    }
    leds[i] = CRGB(out[0], out[1], out[2]);
  }
}

bool topSparkle = false, bottomSparkle = false;     // snow: sparkle on the top / bottom strip
bool leftSparkle = false, rightSparkle = false;     // ...and on the bar that carries the snow risk, while snow is what raises it

// Sparkle on the first `lit` pixels of the strip.
void stepSparkle(const Strip& s, bool on, float dt, int lit) {
  for (int i = 0; i < s.len; i++) {
    float& v = sparkleOf[s.first + i];
    v -= dt / SPARKLE_SECONDS;
    if (v < 0.0f || i >= lit) v = 0.0f;
  }
  if (!on || lit <= 0) return;
  float expect = SPARKLE_PER_SEC * dt;
  int n = (int)expect;
  if (random(10000) < (long)((expect - n) * 10000.0f)) n++;
  for (int k = 0; k < n; k++) sparkleOf[s.first + random(lit)] = 1.0f;
}

void renderFrame() {
  uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;
  topState.phase = fmodf(topState.phase + breathRateHz(topState.breathPct) * dt, 1.0f);
  advance(bottomMarch, dt);
  for (int i = 0; i < NUM_LEDS; i++) gainOf[i] = 1.0f;
  drawBar(STRIP_LEFT, leftBar);
  drawBar(STRIP_RIGHT, rightBar);
  drawTop(STRIP_TOP, topState);
  drawMarch(STRIP_BOTTOM, bottomMarch);
  stepSparkle(STRIP_TOP, topSparkle, dt, STRIP_TOP.len);
  stepSparkle(STRIP_BOTTOM, bottomSparkle, dt, STRIP_BOTTOM.len);
  stepSparkle(STRIP_LEFT, leftSparkle, dt, STRIP_LEFT.len);      // the whole bar, lit or not
  stepSparkle(STRIP_RIGHT, rightSparkle, dt, STRIP_RIGHT.len);
  ditherOut();
  FastLED.show();
}

// ---- Weather: forecast snapshot and its poll task ------------------------------
WeatherForecast forecast;                  // the snapshot the display reads; guarded by forecastMutex
SemaphoreHandle_t forecastMutex;
volatile bool weatherUpdated = false;      // set by the poll task after a good fetch; loop() re-renders
int precipOverride = -1;                   // serial `precip P` test override; -1 = use the forecast
float highOverride = NAN;                  // serial `high C` test override of the forecast high; NaN = use the forecast
volatile int lastFetchStatus = -1;            // ForecastStatus of the last fetch, -1 = none yet (for `forecast`)
volatile int lastFetchHttp = 0;
volatile float lastFetchSeconds = 0.0f;

// The `time`, sweep and test-suite commands set the system clock to a simulated date. The weather
// forecast belongs to the real date, so everything weather-related (the poll task, forecast
// freshness, which day the top strip shows, the typical high) uses realNow(): the system clock
// plus the offset recorded when the clock was last set by hand. 0 when the clock is untouched.
volatile int32_t simClockOffset = 0;
time_t realNow() { return time(nullptr) + simClockOffset; }

TempPrecip weatherTempPrecip(ForecastDay day, time_t now) {
  xSemaphoreTake(forecastMutex, portMAX_DELAY);
  TempPrecip r = forecast.tempPrecip(day, now);
  xSemaphoreGive(forecastMutex);
  return r;
}

// ---- City snow emergencies (Phase S; docs/snow-emergency-feed.md) -----------------
const char* const SNOW_URL = "https://www.somervillema.gov/news.xml";
const uint32_t EMERGENCY_POLL_MS = 3600000UL;     // hourly
const uint32_t EMERGENCY_RETRY_MS = 300000UL;     // after a failed fetch or a body that is not a feed

SnowEmergency emergency;                          // the city's emergency, as the feed tells it; guarded by emergencyMutex
SemaphoreHandle_t emergencyMutex;                 // written on core 0 by the poll, read on core 1 by the renderer
RssScanner rss;                                   // about 7 KB: a global, not on the task stack
volatile bool emergencyUpdated = false;           // set by the poll when the state changed; loop() re-renders
enum { EM_NONE_YET = -1, EM_OK = 0, EM_FETCH_FAILED = 1, EM_NOT_A_FEED = 2 };
volatile int lastEmergencyStatus = EM_NONE_YET;
volatile int lastEmergencyHttp = 0;
volatile int lastEmergencyNotices = 0;
volatile time_t lastEmergencyAt = 0;

// The declaration scrolls out of the 10-item feed within days, so the state is kept in NVS.
Preferences prefs;
void saveEmergencyState(const SnowEmergencyState& st) {
  prefs.begin("snowem", false);
  prefs.putLong64("start", (int64_t)st.start);
  prefs.putLong64("end", (int64_t)st.end);
  prefs.end();
}
void restoreEmergencyState() {
  prefs.begin("snowem", false);
  emergency.state.start = (time_t)prefs.getLong64("start", 0);
  emergency.state.end = (time_t)prefs.getLong64("end", 0);
  prefs.end();
}

// One poll of the city feed. Returns true when the feed was read. A failed fetch, or a body that is not a
// feed (an error page), leaves the state alone. Runs in weatherTask, after the forecast fetch, so only
// one TLS session is open at a time.
bool pollSnowEmergency() {
  HttpsForecastSource feed;
  if (!feed.open(SNOW_URL, NWS_USER_AGENT, 20000, nullptr, "application/rss+xml")) {
    lastEmergencyStatus = EM_FETCH_FAILED;
    lastEmergencyHttp = feed.httpStatus();
    Serial.printf("snow emergency: fetch failed (http %d); trying again in %lu s\n", lastEmergencyHttp,
                  (unsigned long)(EMERGENCY_RETRY_MS / 1000UL));
    return false;
  }
  DechunkingSource body(feed);                    // in case the server chunks the response
  rss.reset();
  scanRss(body, rss);
  lastEmergencyHttp = feed.httpStatus();
  feed.close();
  if (!rss.sawFeed()) {
    lastEmergencyStatus = EM_NOT_A_FEED;
    Serial.printf("snow emergency: not a feed (http %d, %u bytes); trying again in %lu s\n", lastEmergencyHttp,
                  (unsigned)rss.bytes(), (unsigned long)(EMERGENCY_RETRY_MS / 1000UL));
    return false;
  }
  time_t now = realNow();
  xSemaphoreTake(emergencyMutex, portMAX_DELAY);
  SnowEmergency next = emergency;
  xSemaphoreGive(emergencyMutex);
  int notices = next.update(rss, now);
  bool changed = next.state.start != emergency.state.start || next.state.end != emergency.state.end;
  xSemaphoreTake(emergencyMutex, portMAX_DELAY);
  emergency = next;
  xSemaphoreGive(emergencyMutex);
  lastEmergencyStatus = EM_OK;
  lastEmergencyNotices = notices;
  lastEmergencyAt = now;
  Serial.printf("snow emergency: read %d feed items, %d snow notices%s\n", rss.count(), notices, changed ? ", state changed" : "");
  if (changed) {
    saveEmergencyState(next.state);
    emergencyUpdated = true;
  }
  return true;
}

// Runs on core 0 so that the blocking HTTPS fetch (several seconds) never stalls the renderer
// on core 1. Hourly after a good fetch; every 5 minutes after a failed one. A failed fetch
// leaves the previous snapshot in place (it goes stale after 12 hours: see weather-forecast.h).
void weatherTask(void*) {
  WeatherForecast fetched;                 // private to this task; copied into `forecast` when good
  int failures = 0;
  uint32_t forecastDue = 0, emergencyDue = 0;    // millis() times of the next fetches
  for (;;) {
    uint32_t waitMs = 5000;
    time_t now = realNow();
    bool ready = WiFi.status() == WL_CONNECTED && now >= CLOCK_VALID_AFTER;   // else look again shortly
    if (ready && (int32_t)(millis() - forecastDue) >= 0) {
      Serial.println("weather: fetching");
      uint32_t t0 = millis();
      HttpsForecastSource raw;
      raw.open(NWS_URL, NWS_USER_AGENT);   // a failed open reads as empty, so update() reports an io error
      DechunkingSource feed(raw);          // removes chunked transfer encoding if the server used it
      ForecastStatus st = fetched.update(feed, now);
      lastFetchStatus = st;
      lastFetchHttp = raw.httpStatus();
      lastFetchSeconds = (millis() - t0) / 1000.0f;
      if (st == FORECAST_OK) {
        xSemaphoreTake(forecastMutex, portMAX_DELAY);
        forecast = fetched;
        xSemaphoreGive(forecastMutex);
        weatherUpdated = true;
        failures = 0;
        waitMs = WEATHER_POLL_MS;
        Serial.printf("weather: forecast updated in %.1f s, free heap %u\n", lastFetchSeconds, (unsigned)ESP.getFreeHeap());
      } else {
        // Retry soon at first (after a boot WiFi or DNS may not be settled), then back off to the normal retry.
        static const uint32_t BACKOFF_MS[] = { 15000UL, 30000UL, 60000UL, 120000UL };
        waitMs = failures < 4 ? BACKOFF_MS[failures] : WEATHER_RETRY_MS;
        failures++;
        Serial.printf("weather: fetch failed after %.1f s (%s, http %d, free heap %u); trying again in %lu s\n",
                      lastFetchSeconds, forecastStatusName(st), lastFetchHttp, (unsigned)ESP.getFreeHeap(),
                      (unsigned long)(waitMs / 1000UL));
        char shown[121];                   // what the server actually sent, to diagnose a bad body
        size_t k = 0;
        for (const char* h = raw.head(); *h && k < sizeof shown - 1; h++) shown[k++] = (*h >= 32 && *h < 127) ? *h : '.';
        shown[k] = 0;
        Serial.printf("weather: received %u bytes (%s), parsed %u; begins: \"%s\"\n", (unsigned)raw.bytesRead(),
                      feed.chunked() ? "chunked" : "not chunked", (unsigned)feed.bytesOut(), shown);
      }
      raw.close();
      forecastDue = millis() + waitMs;
    }
    if (ready && (int32_t)(millis() - emergencyDue) >= 0) {
      bool ok = pollSnowEmergency();
      emergencyDue = millis() + (ok ? EMERGENCY_POLL_MS : EMERGENCY_RETRY_MS);
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

// `snow P IN H` serial override of the snow forecast (start is an absolute time on the system clock,
// so a `time` sweep moves through it).
struct SnowOverride { bool active; int percent; float inches; time_t start; };
SnowOverride snowOverride = { false, 0, 0.0f, 0 };

// `emergency start|stop WHEN|off` serial override of the city's snow emergency, for testing and manual
// control. Absolute times on the system clock, so a `time` sweep moves through them. Not saved.
bool emergencyOverride = false;
SnowEmergencyState emergencyOverrideState = { 0, 0 };

// The snow emergency the display uses: the override, else the city's (from the feed), which like the real
// forecast is left out while the clock is simulated. False when there is none.
bool currentEmergency(SnowEmergencyState* st) {
  if (emergencyOverride) { *st = emergencyOverrideState; return st->start != 0; }
  st->start = 0; st->end = 0;
  if (simClockOffset != 0) return false;
  xSemaphoreTake(emergencyMutex, portMAX_DELAY);
  *st = emergency.state;
  xSemaphoreGive(emergencyMutex);
  return st->start != 0;
}

SnowEmergencyPhase emergencyPhase(const SnowEmergencyState& st, time_t now) {
  SnowEmergency tmp;
  tmp.state = st;
  return tmp.phase(now);
}

// The snow events the bars use: the snow override, else the NWS forecast for today and the next three days
// (at least 72 hours); both left out while the clock is simulated (time, sweep, test-suite), so a real
// storm does not leak into the simulated dates (the snow override always applies). Then the city's snow
// emergency (1.2): while it is declared or in effect it is a full-size event (100%, 4 inches) starting when the
// parking ban starts, so the risk is the cleaning-risk curve up to the ban and 90 during it. Once it has ended,
// forecast events that started before its end are the same storm and are dropped, so they cannot hold the bar
// up until midnight. `*emergencyLast` tells whether the last event is the emergency.
const int MAX_SNOW_EVENTS = 6;
int currentSnowEvents(SnowEvent* out, int maxOut, time_t now, bool* emergencyLast) {   // (plain types: the generated prototype comes before the constants)
  int n = 0;
  *emergencyLast = false;
  if (snowOverride.active) {
    if (snowOverride.inches > 0.0f && n < maxOut) {
      out[n].any = true;
      out[n].inches = snowOverride.inches;
      out[n].percent = snowOverride.percent;
      out[n].start = snowOverride.start;
      n++;
    }
  } else if (simClockOffset == 0) {
    time_t wn = realNow();
    SnowOutlook days[FORECAST_DAYS_USED];
    xSemaphoreTake(forecastMutex, portMAX_DELAY);
    for (int d = 0; d < FORECAST_DAYS_USED; d++) days[d] = forecast.snow((ForecastDay)d, wn);
    xSemaphoreGive(forecastMutex);
    n = snowEventsFrom(days, FORECAST_DAYS_USED, out, maxOut);
  }
  SnowEmergencyState es;
  if (currentEmergency(&es)) {
    if (emergencyPhase(es, now) != SNOW_EMERGENCY_NONE) {
      if (n < maxOut) {
        out[n].any = true;
        out[n].inches = SNOW_EMERGENCY_INCHES;
        out[n].percent = 100;
        out[n].start = es.start;
        n++;
        *emergencyLast = true;
      }
    } else if (es.end > es.start && es.end <= now) {
      int k = 0;
      for (int i = 0; i < n; i++) if (out[i].start >= es.end) out[k++] = out[i];
      n = k;
    }
  }
  return n;
}

// 3.3.1: snow on the day the top strip shows.
bool topSnowForecast(ForecastDay day, time_t wnow) {
  if (snowOverride.active) return snowOverride.inches > 0.0f;
  xSemaphoreTake(forecastMutex, portMAX_DELAY);
  bool r = forecast.snow(day, wnow).snowForecast();
  xSemaphoreGive(forecastMutex);
  return r;
}

// Typical high (deg C) for the calendar date `dayOffset` days after the one containing `now`.
float typicalHighForDay(time_t now, int dayOffset) {
  struct tm t;
  localtime_r(&now, &t);
  t.tm_mday += dayOffset;
  t.tm_hour = 12; t.tm_min = 0; t.tm_sec = 0;
  t.tm_isdst = -1;
  mktime(&t);                               // normalizes month and day across month ends
  return typicalHighC(t.tm_mon + 1, t.tm_mday);
}

void printForecast() {
  xSemaphoreTake(forecastMutex, portMAX_DELAY);
  WeatherForecast snap = forecast;
  xSemaphoreGive(forecastMutex);
  time_t now = realNow();
  if (lastFetchStatus < 0) Serial.println("forecast: no fetch attempted yet (waiting for WiFi and the clock)");
  else Serial.printf("forecast: last fetch %s in %.1f s, http %d\n", forecastStatusName((ForecastStatus)lastFetchStatus), (float)lastFetchSeconds, (int)lastFetchHttp);
  Serial.printf("  wifi %s, free heap %u\n", WiFi.status() == WL_CONNECTED ? "up" : "down", (unsigned)ESP.getFreeHeap());
  if (!snap.hasData()) { Serial.println("forecast: no good data yet"); return; }
  Serial.printf("forecast: fetched %ld min ago\n", (long)((now - snap.lastGoodAt()) / 60));
  static const char* const DAY_NAMES[FORECAST_DAYS_USED] = { "today", "tomorrow", "day 2", "day 3" };
  for (int d = 0; d < FORECAST_DAYS_USED; d++) {
    TempPrecip t = snap.tempPrecip((ForecastDay)d, now);
    if (!t.valid) { Serial.printf("  %s: no fresh data\n", DAY_NAMES[d]); continue; }
    float typical = typicalHighForDay(now, d);
    Serial.printf("  %s: high %.1f C (typical %.1f C, %+.1f), precipitation %d%%\n", DAY_NAMES[d],
                  t.highC, typical, t.highC - typical, t.precipPercent);
    SnowOutlook sn = snap.snow((ForecastDay)d, now);
    if (sn.snowForecast()) {
      char when[40];
      fmtTime(sn.startTime, when, sizeof when);
      Serial.printf("    snow: %.1f in, %d%%, starting %s\n", sn.amountInches(), sn.probabilityPercent, when);
    } else {
      Serial.println("    snow: none");
    }
  }
}

// One line: label, phase, start and end of a snow emergency state.
void printEmergencyState(const char* label, const SnowEmergencyState& st, time_t now) {
  if (st.start == 0) { Serial.printf("%s: none\n", label); return; }
  char a[40], b[40];
  fmtTime(st.start, a, sizeof a);
  if (st.end) fmtTime(st.end, b, sizeof b); else strcpy(b, "not announced");
  SnowEmergencyPhase ph = emergencyPhase(st, now);
  Serial.printf("%s: %s, ban starts %s, ends %s\n", label, ph != SNOW_EMERGENCY_NONE ? snowEmergencyPhaseName(ph) : st.end ? "ended" : "expired", a, b);
}

void printEmergency() {
  time_t now = time(nullptr);
  xSemaphoreTake(emergencyMutex, portMAX_DELAY);
  SnowEmergencyState city = emergency.state;
  xSemaphoreGive(emergencyMutex);
  printEmergencyState("emergency (city feed)", city, realNow());
  if (emergencyOverride) printEmergencyState("emergency (override)", emergencyOverrideState, now);
  else Serial.println("emergency override: off");
  SnowEmergencyState used;
  if (currentEmergency(&used)) printEmergencyState("emergency in use", used, now);
  else Serial.printf("emergency in use: none%s\n", simClockOffset != 0 && !emergencyOverride ? " (the city feed is left out while the clock is simulated)" : "");
  if (lastEmergencyStatus == EM_NONE_YET) Serial.println("emergency poll: none yet (waiting for WiFi and the clock)");
  else if (lastEmergencyStatus == EM_OK) {
    Serial.printf("emergency poll: ok %ld min ago, %d snow notices in the feed\n", (long)((realNow() - lastEmergencyAt) / 60), (int)lastEmergencyNotices);
  } else {
    Serial.printf("emergency poll: %s (http %d)\n", lastEmergencyStatus == EM_FETCH_FAILED ? "fetch failed" : "not a feed", (int)lastEmergencyHttp);
  }
}

// WHEN for the emergency command: `now`, +H or -H (hours from now, fractions allowed), or
// YYYY-MM-DD HH:MM[:SS] in US Eastern local time.
bool parseWhen(const char* s, time_t* out) {
  while (*s == ' ') s++;
  if (strncmp(s, "now", 3) == 0) { *out = time(nullptr); return true; }
  if (*s == '+' || *s == '-') {
    char* end;
    double h = strtod(s, &end);
    if (end == s || *end) return false;
    *out = time(nullptr) + (time_t)(h * 3600.0);
    return true;
  }
  return parseDateTime(s, out) != nullptr;
}

// ---- Clock-driven display -----------------------------------------------------
bool clockMode = true;          // display follows the clock until a manual command
bool clockRendered = false;     // true once a valid clock has been rendered
uint32_t lastClockUpdateMs = 0;

void setClockTo(time_t t) {
  time_t real = realNow();                  // keep the real date for the weather, whatever is simulated
  struct timeval tv = { t, 0 };
  settimeofday(&tv, nullptr);
  simClockOffset = (int32_t)(real - t);
}

// Parses "YYYY-MM-DD HH:MM[:SS]" (US Eastern local time) at s. Returns the position just
// past it, or nullptr if it doesn't parse.
const char* parseDateTime(const char* s, time_t* out) {
  int Y, M, D, h, m, n = 0, sec = 0;
  if (sscanf(s, "%d-%d-%d %d:%d%n", &Y, &M, &D, &h, &m, &n) < 5 || n == 0) return nullptr;
  s += n;
  int k = 0;
  if (*s == ':' && sscanf(s, ":%d%n", &sec, &k) == 1) s += k;
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = sec;
  t.tm_isdst = -1;
  *out = mktime(&t);
  return s;
}

// Sweep (time START END): the clock steps one hour per second from START through END.
bool sweepActive = false;
time_t sweepNext = 0, sweepEnd = 0;
uint32_t sweepLastMs = 0;

void sweepStep() {
  sweepLastMs = millis();
  setClockTo(sweepNext);
  updateFromClock(true);
  sweepNext += 3600;
  if (sweepNext > sweepEnd) {
    sweepActive = false;
    Serial.println("sweep done");
  }
}

// Test suite (test-suite): a fixed list of dates, one every 6 seconds.
// when: YYYY-MM-DD HH:MM, US Eastern. emStart / emEnd (optional): a snow emergency forced for that case only.
struct TestCase { const char* when; const char* label; const char* emStart; const char* emEnd; };
const TestCase TEST_SUITE[] = {
  { "2026-10-02 18:00", "unremarkable" },
  { "2026-09-08 15:00", "trash delayed by holiday" },
  { "2026-10-06 15:00", "trash night" },
  { "2026-10-27 18:00", "yard waste" },
  { "2026-10-22 20:00", "street cleaning left" },
  { "2026-09-30 20:00", "street cleaning right" },
  { "2026-10-14 18:00", "yard waste and street cleaning" },
  { "2026-09-10 11:00", "trash collection and street cleaning" },
  { "2026-05-06 18:00", "street cleaning imminent both sides" },
  { "2026-10-23 18:00", "street cleaning left very far away" },
  { "2026-12-10 18:00", "snow emergency declared, ban starts in 10 hours", "2026-12-11 04:00", nullptr },
  { "2026-12-11 08:00", "snow emergency in effect", "2026-12-11 04:00", nullptr },
  { "2026-12-12 12:00", "snow emergency ended", "2026-12-11 04:00", "2026-12-12 06:00" },
};
const int NUM_TESTS = sizeof(TEST_SUITE) / sizeof(TEST_SUITE[0]);
const uint32_t SUITE_STEP_MS = 6000;

bool suiteActive = false;
int suiteIndex = 0;
uint32_t suiteLastMs = 0;
bool suiteSavedOverride = false;                 // the emergency override from before the suite
SnowEmergencyState suiteSavedState = { 0, 0 };

void suiteRestore() {                            // the suite's own emergency cases must not outlive it
  emergencyOverride = suiteSavedOverride;
  emergencyOverrideState = suiteSavedState;
}

void suiteStep() {
  suiteLastMs = millis();
  if (suiteIndex >= NUM_TESTS) {          // the last case has been shown for its full time
    suiteActive = false;
    suiteRestore();
    Serial.println("\ntest-suite done");
    return;
  }
  const TestCase& t = TEST_SUITE[suiteIndex];
  time_t when;
  if (!parseDateTime(t.when, &when)) {
    Serial.printf("test-suite: bad date '%s'\n", t.when);
    suiteActive = false;
    suiteRestore();
    return;
  }
  if (t.emStart) {                                 // this case forces an emergency
    time_t es = 0, ee = 0;
    if (!parseDateTime(t.emStart, &es) || (t.emEnd && !parseDateTime(t.emEnd, &ee))) {
      Serial.printf("test-suite: bad emergency time in '%s'\n", t.label);
      suiteActive = false;
      suiteRestore();
      return;
    }
    emergencyOverrideState.start = es;
    emergencyOverrideState.end = ee;
    emergencyOverride = true;
  } else {
    suiteRestore();
  }
  Serial.printf("\n=== test-suite %d/%d: %s  %s ===\n", suiteIndex + 1, NUM_TESTS, t.when, t.label);
  setClockTo(when);
  updateFromClock(true);
  suiteIndex++;
}

void fmtTime(time_t t, char* buf, size_t n) {
  struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(buf, n, "%a %Y-%m-%d %H:%M %Z", &tmv);
}

void reportSide(const char* name, time_t end, time_t now, int risk) {
  char when[40];
  fmtTime(end, when, sizeof when);
  Serial.printf("%s: cleaning ends %s, %.1f h remaining, risk %d\n",
                name, when, (int64_t)(end - now) / 3600.0f, risk);
}

void updateFromClock(bool verbose) {
  time_t now = time(nullptr);
  if (now < CLOCK_VALID_AFTER) {
    if (verbose) Serial.println("clock not set yet (waiting for NTP, or use: time YYYY-MM-DD HH:MM)");
    return;
  }
  time_t endL = nextCleaningEnd(LEFT_SCHEDULE, now);
  time_t endR = nextCleaningEnd(RIGHT_SCHEDULE, now);
  // end == 0 (nothing found) must read as "far away", not as a huge negative remaining time
  int riskL = riskFromHours(endL ? (int64_t)(endL - now) / 3600.0f : 504.0f);
  int riskR = riskFromHours(endR ? (int64_t)(endR - now) / 3600.0f : 504.0f);
  int cleanL = riskL, cleanR = riskR;      // street cleaning alone, for the report
  // 1.2: a forecast snowstorm puts the side where parking is banned during an emergency (alternating
  // by winter) at the snow risk, if that is higher than its cleaning risk. The other side is unaffected.
  struct tm nt;
  localtime_r(&now, &nt);
  bool snowOnRight = snowRiskOnRightBar(nt.tm_year + 1900, nt.tm_mon + 1);
  SnowEvent events[MAX_SNOW_EVENTS];
  bool emergencyLast;
  int nEvents = currentSnowEvents(events, MAX_SNOW_EVENTS, now, &emergencyLast);
  SnowEvent snow = { false, 0.0f, 0, 0 };               // the event with the highest risk
  int snowRisk = 0;
  bool snowIsEmergency = false;
  for (int i = 0; i < nEvents; i++) {
    int r = snowRiskFrom(events[i], riskFromHours((int64_t)(events[i].start - now) / 3600.0f));
    bool isEm = emergencyLast && i == nEvents - 1;
    if (i == 0 || r > snowRisk || (r == snowRisk && isEm)) { snowRisk = r; snow = events[i]; snowIsEmergency = isEm; }   // a tie shows the city's emergency
  }
  if (snowOnRight) riskR = max(riskR, snowRisk);
  else             riskL = max(riskL, snowRisk);
  setLeft(riskL, riskColor(riskL));
  setRight(riskR, riskColor(riskR));

  // Top and bottom: waste color (white when nothing to put out); above the threshold, march away from the riskier side.
  int hi = max(riskL, riskR), speed = 0;
  if (hi > MARCH_THRESHOLD && riskL != riskR) {
    int mag = MARCH_MIN_SPEED + (hi - MARCH_THRESHOLD) * (MARCH_MAX_SPEED - MARCH_MIN_SPEED) / (90 - MARCH_THRESHOLD);
    speed = (riskL > riskR) ? mag : -mag;    // left riskier -> march right (positive)
  }
  setBottom(wasteCollectionColors(now), speed);
  bottomSparkle = snowRisk > MARCH_THRESHOLD;   // 1.2: snow risk high enough to march adds a sparkle to the bottom strip
  rightSparkle = snowOnRight && snowRisk > cleanR;   // and to the whole snow bar whenever snow, not street
  leftSparkle = !snowOnRight && snowRisk > cleanL;   // cleaning, sets its risk

  // Top: the weather forecast, today's or (from 5 pm) tomorrow's. Dark when there is no fresh forecast.
  time_t wnow = realNow();                  // real time even while the clock is simulated
  struct tm lt;
  localtime_r(&wnow, &lt);
  ForecastDay day = lt.tm_hour >= TOMORROW_AFTER_HOUR ? FORECAST_TOMORROW : FORECAST_TODAY;
  TempPrecip wx = weatherTempPrecip(day, wnow);
  bool haveWx = wx.valid || precipOverride >= 0 || !std::isnan(highOverride);
  int rawPrecip = precipOverride >= 0 ? precipOverride : max(wx.precipPercent, 0);
  int precip = effectivePrecip(rawPrecip);
  float highC = !std::isnan(highOverride) ? highOverride : wx.highC;     // NaN when the feed has no high for that day
  float typicalC = typicalHighForDay(wnow, day == FORECAST_TOMORROW ? 1 : 0);
  bool haveDelta = !std::isnan(highC) && !std::isnan(typicalC);
  float deltaC = haveDelta ? highC - typicalC : 0.0f;
  if (haveWx) {
    Rgb img[TOP_PIXELS];
    weatherTopImage(haveDelta, deltaC, img);
    CRGB base[TOP_PIXELS];
    for (int i = 0; i < TOP_PIXELS; i++) base[i] = CRGB(img[i].r, img[i].g, img[i].b);
    setTop(base, precip);
  } else {
    setTopDark();
  }

  topSparkle = topSnowForecast(day, wnow);      // 3.3.1: any snow forecast for the displayed day
  clockMode = true;
  clockRendered = true;
  lastClockUpdateMs = millis();
  if (verbose) {
    char buf[40];
    fmtTime(now, buf, sizeof buf);
    Serial.printf("now %s\n", buf);
    reportSide("left ", endL, now, cleanL);
    reportSide("right", endR, now, cleanR);
    Serial.printf("bottom speed %d\n", speed);
    if (haveWx) {
      Serial.printf("top: %s forecast, precipitation %d%%%s, breathing %.2f Hz\n",
                    day == FORECAST_TOMORROW ? "tomorrow's" : "today's",
                    rawPrecip, precipOverride >= 0 ? " (overridden)" : "", breathRateHz(precip));
      if (haveDelta) {
        TopShape sh = topShapeFor(deltaC);
        Serial.printf("top: high %.1f C%s vs typical %.1f C (%+.1f): %s\n", highC,
                      !std::isnan(highOverride) ? " (overridden)" : "", typicalC, deltaC,
                      sh.hotPixels ? "hot" : sh.coldPixels ? "cold" : "about typical");
        if (sh.hotPixels || sh.coldPixels)
          Serial.printf("top: %d %s pixels, bleed depth %d into the center\n",
                        sh.hotPixels + sh.coldPixels, sh.hotPixels ? "scarlett" : "sky blue", sh.hotBleed + sh.coldBleed);
      } else {
        Serial.println("top: no forecast high, default display");
      }
    } else Serial.println("top: no forecast, dark");
    if (snow.any) {
      Serial.printf("snow: %.1f in, %d%%, starts in %.1f h -> snow risk %d on the %s bar%s; bars show left %d, right %d; sparkle: bottom %s, left %s, right %s\n",
                    snow.inches, snow.percent, (snow.start - now) / 3600.0f, snowRisk, snowOnRight ? "right" : "left",
                    snowIsEmergency ? " (city snow emergency)" : snowOverride.active ? " (overridden)" : "", riskL, riskR, bottomSparkle ? "on" : "off", leftSparkle ? "on" : "off", rightSparkle ? "on" : "off");
    } else Serial.println("snow: none forecast");
    SnowEmergencyState esv;
    if (currentEmergency(&esv)) printEmergencyState("emergency", esv, now);
    Serial.printf("top sparkle %s\n", topSparkle ? "on" : "off");
    const char* why = nullptr;
    WasteState ws = wasteState(now, &why);
    Serial.printf("waste: %s%s%s%s\n",
                  ws == WASTE_PUT_OUT ? "put out now" : ws == WASTE_PUT_OUT_YARD ? "put out now, with yard waste"
                  : ws == WASTE_SKIPPED ? "skipped (holiday)" : "idle",
                  why ? " [delayed by " : "", why ? why : "", why ? "]" : "");
  }
}

// Waste colors for the top and bottom strips. Luminance-matched to the parking ramp
// (Y = 0.2126) and chosen to avoid every hue the left/right ramp uses (118 deg green
// through amber to 0 deg red), so nothing on top/bottom reads as parking.
const CRGB WASTE_COLOR_TRASH    = CRGB(  0,  50, 252);   // azure (hue 228): put the trash out
const CRGB WASTE_COLOR_YARD     = CRGB(  0,  50, 252);   // azure (hue 228): trash and yard waste, the ants
const CRGB WASTE_COLOR_YARD_ALT = CRGB(  0,  72,  36);   // spring green (hue 150): yard waste, the background between the ants
const CRGB WASTE_COLOR_SKIPPED  = CRGB( 90,  35,  60);   // pink: holiday delay, don't put it out yet
const CRGB WASTE_COLOR_STANDARD = CRGB( 54,  54,  54);   // white: nothing to put out

// Colors of the top/bottom pattern. Most states are one color (the ants dimmed in between);
// yard waste alternates between two colors, since you put out trash AND yard waste.
AntColors wasteCollectionColors(time_t now) {
  switch (wasteState(now)) {
    case WASTE_PUT_OUT:      return antsOf(WASTE_COLOR_TRASH);
    case WASTE_PUT_OUT_YARD: return { WASTE_COLOR_YARD, WASTE_COLOR_YARD_ALT };
    case WASTE_SKIPPED:      return antsOf(WASTE_COLOR_SKIPPED);
    default:                 return antsOf(WASTE_COLOR_STANDARD);
  }
}

// ---- Serial -------------------------------------------------------------------
CRGB current = CRGB::White;

bool setColor(char c) {
  switch (c) {
    case 'r': current = CRGB::Red;     return true;
    case 'g': current = CRGB::Green;   return true;
    case 'b': current = CRGB::Blue;    return true;
    case 'w': current = CRGB::White;   return true;
    case 'y': current = CRGB::Yellow;  return true;
    case 'c': current = CRGB::Cyan;    return true;
    case 'm': current = CRGB::Magenta; return true;
  }
  return false;
}

void allOff() {
  clockMode = false;
  setLeft(0, CRGB::Black);
  setRight(0, CRGB::Black);
  setTopDark();
  setBottom(CRGB::Black, 0);
}

void handle(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();
  if (cmd != "status" && cmd != "risk" && cmd != "precip" && cmd != "high" && cmd != "snow" && cmd != "emergency" && cmd != "forecast") {
    sweepActive = false;
    if (suiteActive) { suiteActive = false; suiteRestore(); }
  }   // any other command ends a sweep or test suite

  if (cmd == "left")        { clockMode = false; setLeft(arg.toInt(), current);   Serial.printf("left %d\n", leftBar.n); }
  else if (cmd == "right")  { clockMode = false; setRight(arg.toInt(), current);  Serial.printf("right %d\n", rightBar.n); }
  else if (cmd == "bottom") { clockMode = false; setBottom(current, arg.toInt()); Serial.printf("bottom speed %d\n", bottomMarch.speed); }
  else if (cmd == "off")    { allOff(); Serial.println("off"); }
  else if (cmd == "precip") {
    if (arg == "off") precipOverride = -1;
    else if (arg.length()) precipOverride = constrain(arg.toInt(), 0, 100);
    if (precipOverride >= 0) Serial.printf("precip override %d%%\n", precipOverride);
    else Serial.println("precip override off (using the forecast)");
    updateFromClock(true);
  }
  else if (cmd == "high") {
    if (arg == "off") highOverride = NAN;
    else if (arg.length()) highOverride = constrain(arg.toFloat(), -60.0f, 60.0f);
    if (!std::isnan(highOverride)) Serial.printf("high override %.1f C\n", highOverride);
    else Serial.println("high override off (using the forecast)");
    updateFromClock(true);
  }
  else if (cmd == "snow") {
    if (arg == "off") { snowOverride.active = false; Serial.println("snow override off (using the forecast)"); }
    else if (arg.length()) {
      int p; float inches, hours;
      if (sscanf(arg.c_str(), "%d %f %f", &p, &inches, &hours) != 3) {
        Serial.println("usage: snow PERCENT INCHES HOURS   (HOURS until the snow starts, negative if already falling) | snow off");
        return;
      }
      snowOverride.active = true;
      snowOverride.percent = constrain(p, 0, 100);
      snowOverride.inches = constrain(inches, 0.0f, 60.0f);
      snowOverride.start = time(nullptr) + (time_t)(constrain(hours, -48.0f, 504.0f) * 3600.0f);
      Serial.printf("snow override: %d%%, %.1f in, starting in %.1f h\n", snowOverride.percent, snowOverride.inches,
                    constrain(hours, -48.0f, 504.0f));
    }
    updateFromClock(true);
  }
  else if (cmd == "emergency") {
    String a = arg;
    a.trim();
    if (a.length() == 0) { printEmergency(); return; }
    String sub = a, rest = "";
    int sp2 = a.indexOf(' ');
    if (sp2 >= 0) { sub = a.substring(0, sp2); rest = a.substring(sp2 + 1); rest.trim(); }
    if (sub == "off") {
      emergencyOverride = false;
      Serial.println("emergency override off (using the city feed)");
    } else if (sub == "start" || sub == "stop") {
      time_t when;
      if (!parseWhen(rest.c_str(), &when)) {
        Serial.println("usage: emergency start|stop WHEN | emergency off | emergency   (WHEN: now, +H, -H, or YYYY-MM-DD HH:MM)");
        return;
      }
      if (sub == "start") {
        emergencyOverrideState.start = when;
        emergencyOverrideState.end = 0;
      } else {
        SnowEmergencyState base;
        if (!currentEmergency(&base)) { Serial.println("no emergency to stop; use: emergency start WHEN"); return; }
        if (when <= base.start) { Serial.println("the end must be after the start"); return; }
        emergencyOverrideState.start = base.start;
        emergencyOverrideState.end = when;
      }
      emergencyOverride = true;
    } else {
      Serial.println("usage: emergency start|stop WHEN | emergency off | emergency   (WHEN: now, +H, -H, or YYYY-MM-DD HH:MM)");
      return;
    }
    printEmergency();
    updateFromClock(true);
  }
  else if (cmd == "forecast") printForecast();
  else if (cmd == "color")  {
    if (arg.length() && setColor(arg[0])) Serial.printf("color %c\n", arg[0]);
    else Serial.println("color: r g b w y c m");
  }
  else if (cmd == "bright") {
    bright = constrain(arg.toInt(), MIN_BRIGHT, MAX_BRIGHT);
    Serial.printf("brightness %d (cap %d)\n", bright, MAX_BRIGHT);
  }
  else if (cmd == "time") {
    if (arg.length()) {
      time_t t1, t2;
      const char* p = parseDateTime(arg.c_str(), &t1);
      if (!p) {
        Serial.println("usage: time [YYYY-MM-DD HH:MM[:SS]] [END]   (US Eastern local time)");
        return;
      }
      while (*p == ' ') p++;
      if (*p) {                                      // a second time: sweep hour by hour
        if (!parseDateTime(p, &t2) || t2 < t1) {
          Serial.println("usage: time START END   (both YYYY-MM-DD HH:MM[:SS], END not before START)");
          return;
        }
        sweepNext = t1;
        sweepEnd = t2;
        sweepActive = true;
        Serial.printf("sweep: %ld hourly steps, one per second; any other command stops it\n",
                      (long)((t2 - t1) / 3600 + 1));
        sweepStep();
        return;
      }
      setClockTo(t1);
    }
    updateFromClock(true);
  }
  else if (cmd == "test-suite") {
    if (!suiteActive) { suiteSavedOverride = emergencyOverride; suiteSavedState = emergencyOverrideState; }
    suiteIndex = 0;
    suiteActive = true;
    Serial.printf("test-suite: %d cases, one every %lu s; any other command stops it\n",
                  NUM_TESTS, (unsigned long)(SUITE_STEP_MS / 1000));
    suiteStep();
  }
  else if (cmd == "risk") {
    float r = arg.toFloat();
    int k = riskFromHours(r);
    CRGB c = riskColor(k);
    Serial.printf("remaining %.1f h -> risk %d, color %d,%d,%d\n", r, k, c.r, c.g, c.b);
  }
  else if (cmd == "status") {
    Serial.printf("brightness %d, color %d,%d,%d, left %d, right %d, top precip %d%%, bottom speed %d, clock mode %s, runs 2 x %d, pins %d and %d\n",
                  bright, current.r, current.g, current.b, leftBar.n, rightBar.n,
                  topState.breathPct, bottomMarch.speed, clockMode ? "on" : "off",
                  RUN_LEN, DATA_PIN_A, DATA_PIN_B);
  }
  else Serial.println("commands: left N | right N | bottom S | precip P|off | high C|off | snow P IN H|off | emergency [start|stop WHEN|off] | forecast | color X | bright B | off | time [YYYY-MM-DD HH:MM] | test-suite | risk H | status");
}

String inbuf;

void pollSerial() {
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch == '\n' || ch == '\r') {
      if (inbuf.length()) { handle(inbuf); inbuf = ""; }
    } else if (inbuf.length() < 80) {
      inbuf += ch;
    }
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.begin(WIFI_SSID, WIFI_PASS);                      // non-blocking; the renderer keeps running
  configTzTime(TZ_EASTERN, "pool.ntp.org", "time.nist.gov");
  forecastMutex = xSemaphoreCreateMutex();
  emergencyMutex = xSemaphoreCreateMutex();
  restoreEmergencyState();
  if (emergency.state.start) Serial.println("snow emergency: state restored from flash");
  xTaskCreatePinnedToCore(weatherTask, "weather", 20480, nullptr, 1, nullptr, 0);   // core 0; the Arduino loop runs on core 1
  // Keep handy for testing without wifi
//  setenv("TZ", TZ_EASTERN, 1);
//  tzset();

  FastLED.addLeds<WS2812B, DATA_PIN_A, COLOR_ORDER>(leds, 0, RUN_LEN);
  FastLED.addLeds<WS2812B, DATA_PIN_B, COLOR_ORDER>(leds, RUN_LEN, RUN_LEN);
  FastLED.setBrightness(255);            // brightness is applied in ditherOut, at higher precision
  FastLED.setDither(DISABLE_DITHER);     // ...and so is the dithering
  lastFrameMs = millis();
  updateFromClock(true);
  Serial.println("street-cleaning-reveaLED ready. try: time 2026-10-22 16:30");

}

void loop() {
  pollSerial();
  if (sweepActive && millis() - sweepLastMs >= 1000) sweepStep();
  if (suiteActive && millis() - suiteLastMs >= SUITE_STEP_MS) suiteStep();
  // Retry every second until the first valid clock (NTP may take a while after boot),
  // then refresh once a minute.
  uint32_t every = clockRendered ? 60000 : 1000;
  if (clockMode && millis() - lastClockUpdateMs >= every) {
    lastClockUpdateMs = millis();
    updateFromClock(false);
  }
  if (clockMode && (weatherUpdated || emergencyUpdated)) {   // a new forecast or emergency arrived: show it now, not at the next minute
    weatherUpdated = false;
    emergencyUpdated = false;
    updateFromClock(false);
  }
  renderFrame();
}
