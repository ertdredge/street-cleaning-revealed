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
//
// Commands (manual commands use the current color and switch clock mode off):
//   left N         light N pixels (0..90) from the bottom of the left strip
//   right N        light N pixels (0..90) from the bottom of the right strip
//   top S          top strip pattern, speed S in -100..100 (negative = left, 0 = frozen)
//   bottom S       bottom strip pattern, same scale
//   color r|g|b|w|y|c|m   set the current color (default w)
//   bright B       set brightness, clamped to 8..48
//   off            all dark
//   time [YYYY-MM-DD HH:MM[:SS]]   set the clock (US Eastern local time), then render the
//                  display from it; with no argument, render from the current clock.
//                  Clock mode then re-renders once a minute.
//   risk H         print risk and color for H hours remaining (no rendering)
//   status         print current settings
//
// Protan note: r/g are easy to confuse by eye. Prefer positions and counts.

#include <FastLED.h>
#include <sys/time.h>
#include <time.h>
#include <WiFi.h>
#include "secrets.h"            // const char* WIFI_SSID, WIFI_PASS; add to .gitignore

#define DATA_PIN_A 27       // run A: left (90) -> top (60)
#define DATA_PIN_B 33       // run B: bottom (60) -> right (90)
#define RUN_LEN 150
#define NUM_LEDS (2 * RUN_LEN)
#define COLOR_ORDER GRB     // verified in A1
#define MIN_BRIGHT 8
#define MAX_BRIGHT 48       // bench cap: all 300 pixels white at 64 would be ~1.9 A, too close to the 2 A adapter

// ---- Tunables ---------------------------------------------------------------
const uint32_t FRAME_MS = 20;            // render period (50 fps)
const float ANIM_PERIOD = 6.0f;          // marching pattern repeats every N pixels
const float ANIM_LIT = 2.0f;             // ...of which this many are lit
const uint8_t ANIM_DIM_LEVEL = 40;       // brightness (0..255) of the other pixels; 0 = off
const float ANIM_MAX_PPS = 20.0f;        // pixels per second at speed +-100
const int MARCH_THRESHOLD = 55;          // risk above this animates top and bottom
const int MARCH_MIN_SPEED = 10;          // speed at risk = MARCH_THRESHOLD
const int MARCH_MAX_SPEED = 100;         // speed at risk = 90
const char* const TZ_EASTERN = "EST5EDT,M3.2.0,M11.1.0";
const time_t CLOCK_VALID_AFTER = 1767225600;   // 2026-01-01 UTC; earlier means the clock is unset

// ---- Layer 0: geometry --------------------------------------------------------
struct Strip { uint16_t first; uint16_t len; };
const Strip STRIP_LEFT   = {  0, 90 };   // run A, bottom -> top
const Strip STRIP_TOP    = { 90, 60 };   // run A, left -> right
const Strip STRIP_BOTTOM = {150, 60 };   // run B, left -> right
const Strip STRIP_RIGHT  = {210, 90 };   // run B, bottom -> top

CRGB leds[NUM_LEDS];

// Types used in function signatures. The Arduino build auto-generates prototypes and
// places them before the first function, so these must be defined above it.
// ---- BEGIN TYPES ----
struct Schedule { int wday; int nthA; int nthB; };   // wday: 0 = Sunday .. 6 = Saturday
struct Bar   { int n; CRGB color; };
struct March { CRGB color; int speed; float phase; };
// ---- END TYPES ----

// ---- Layer 1: primitives ------------------------------------------------------
// Logical position 0 is the bottom (left/right strips) or the left end (top/bottom).
CRGB& px(const Strip& s, int pos) {
  return leds[s.first + constrain(pos, 0, (int)s.len - 1)];
}

void fillStrip(const Strip& s, CRGB c) {
  for (int i = 0; i < s.len; i++) leds[s.first + i] = c;
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
// ---- END PURE ------------------------------------------------------------------

// ---- Layer 2: setters (state only) --------------------------------------------

Bar leftBar = {0, CRGB::Black}, rightBar = {0, CRGB::Black};
March topMarch = {CRGB::Black, 0, 0.0f}, bottomMarch = {CRGB::Black, 0, 0.0f};

void setLeft(int n, CRGB c)  { leftBar  = { constrain(n, 0, (int)STRIP_LEFT.len),  c }; }
void setRight(int n, CRGB c) { rightBar = { constrain(n, 0, (int)STRIP_RIGHT.len), c }; }
void setTop(CRGB c, int speed)    { topMarch.color    = c; topMarch.speed    = constrain(speed, -100, 100); }
void setBottom(CRGB c, int speed) { bottomMarch.color = c; bottomMarch.speed = constrain(speed, -100, 100); }

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
  CRGB dim = m.color;
  dim.nscale8_video(ANIM_DIM_LEVEL);
  for (int i = 0; i < s.len; i++) {
    float pos = fmodf(i - m.phase, ANIM_PERIOD);
    if (pos < 0) pos += ANIM_PERIOD;
    px(s, i) = (pos < ANIM_LIT) ? m.color : dim;
  }
}

void renderFrame() {
  uint32_t now = millis();
  if (now - lastFrameMs < FRAME_MS) return;
  float dt = (now - lastFrameMs) / 1000.0f;
  lastFrameMs = now;
  advance(topMarch, dt);
  advance(bottomMarch, dt);
  drawBar(STRIP_LEFT, leftBar);
  drawBar(STRIP_RIGHT, rightBar);
  drawMarch(STRIP_TOP, topMarch);
  drawMarch(STRIP_BOTTOM, bottomMarch);
  FastLED.show();
}

// ---- Clock-driven display -----------------------------------------------------
bool clockMode = true;          // display follows the clock until a manual command
bool clockRendered = false;     // true once a valid clock has been rendered
uint32_t lastClockUpdateMs = 0;

void setClock(int Y, int M, int D, int h, int m, int s) {
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D;
  t.tm_hour = h; t.tm_min = m; t.tm_sec = s;
  t.tm_isdst = -1;
  struct timeval tv = { mktime(&t), 0 };
  settimeofday(&tv, nullptr);
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
  setLeft(riskL, riskColor(riskL));
  setRight(riskR, riskColor(riskR));

  // Top and bottom: standard yellow; above the threshold, march away from the riskier side.
  int hi = max(riskL, riskR), speed = 0;
  if (hi > MARCH_THRESHOLD && riskL != riskR) {
    int mag = MARCH_MIN_SPEED + (hi - MARCH_THRESHOLD) * (MARCH_MAX_SPEED - MARCH_MIN_SPEED) / (90 - MARCH_THRESHOLD);
    speed = (riskL > riskR) ? mag : -mag;    // left riskier -> march right (positive)
  }
  CRGB standard = riskColor(45);
  setTop(standard, speed);
  setBottom(standard, speed);

  clockMode = true;
  clockRendered = true;
  lastClockUpdateMs = millis();
  if (verbose) {
    char buf[40];
    fmtTime(now, buf, sizeof buf);
    Serial.printf("now %s\n", buf);
    reportSide("left ", endL, now, riskL);
    reportSide("right", endR, now, riskR);
    Serial.printf("top/bottom speed %d\n", speed);
  }
}

// ---- Serial -------------------------------------------------------------------
CRGB current = CRGB::White;
uint8_t bright = 32;

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
  setTop(CRGB::Black, 0);
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

  if (cmd == "left")        { clockMode = false; setLeft(arg.toInt(), current);   Serial.printf("left %d\n", leftBar.n); }
  else if (cmd == "right")  { clockMode = false; setRight(arg.toInt(), current);  Serial.printf("right %d\n", rightBar.n); }
  else if (cmd == "top")    { clockMode = false; setTop(current, arg.toInt());    Serial.printf("top speed %d\n", topMarch.speed); }
  else if (cmd == "bottom") { clockMode = false; setBottom(current, arg.toInt()); Serial.printf("bottom speed %d\n", bottomMarch.speed); }
  else if (cmd == "off")    { allOff(); Serial.println("off"); }
  else if (cmd == "color")  {
    if (arg.length() && setColor(arg[0])) Serial.printf("color %c\n", arg[0]);
    else Serial.println("color: r g b w y c m");
  }
  else if (cmd == "bright") {
    bright = constrain(arg.toInt(), MIN_BRIGHT, MAX_BRIGHT);
    FastLED.setBrightness(bright);
    Serial.printf("brightness %d (cap %d)\n", bright, MAX_BRIGHT);
  }
  else if (cmd == "time") {
    if (arg.length()) {
      int Y, M, D, h, m, s = 0;
      if (sscanf(arg.c_str(), "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) < 5) {
        Serial.println("usage: time YYYY-MM-DD HH:MM[:SS]   (US Eastern local time)");
        return;
      }
      setClock(Y, M, D, h, m, s);
    }
    updateFromClock(true);
  }
  else if (cmd == "risk") {
    float r = arg.toFloat();
    int k = riskFromHours(r);
    CRGB c = riskColor(k);
    Serial.printf("remaining %.1f h -> risk %d, color %d,%d,%d\n", r, k, c.r, c.g, c.b);
  }
  else if (cmd == "status") {
    Serial.printf("brightness %d, color %d,%d,%d, left %d, right %d, top speed %d, bottom speed %d, clock mode %s, runs 2 x %d, pins %d and %d\n",
                  bright, current.r, current.g, current.b, leftBar.n, rightBar.n,
                  topMarch.speed, bottomMarch.speed, clockMode ? "on" : "off",
                  RUN_LEN, DATA_PIN_A, DATA_PIN_B);
  }
  else Serial.println("commands: left N | right N | top S | bottom S | color X | bright B | off | time [YYYY-MM-DD HH:MM] | risk H | status");
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
  configTzTime("EST5EDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
  // Keep handy for testing without wifi
//  setenv("TZ", TZ_EASTERN, 1);
//  tzset();

  FastLED.addLeds<WS2812B, DATA_PIN_A, COLOR_ORDER>(leds, 0, RUN_LEN);
  FastLED.addLeds<WS2812B, DATA_PIN_B, COLOR_ORDER>(leds, RUN_LEN, RUN_LEN);
  FastLED.setBrightness(bright);
  lastFrameMs = millis();
  updateFromClock(true);
  Serial.println("street-cleaning-reveaLED ready. try: time 2026-10-22 16:30");

}

void loop() {
  pollSerial();
  // Retry every second until the first valid clock (NTP may take a while after boot),
  // then refresh once a minute.
  uint32_t every = clockRendered ? 60000 : 1000;
  if (clockMode && millis() - lastClockUpdateMs >= every) {
    lastClockUpdateMs = millis();
    updateFromClock(false);
  }
  renderFrame();
}
