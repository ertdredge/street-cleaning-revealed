// A3: multiple pixels. Serial-driven (115200 baud, newline-terminated).
// Purpose: verify addressing along the strip, reach the far end, and hold a
// known load so you can measure supply voltage at the strip input.
//
// Commands:
//   run N        light pixels 0..N-1 in the current color
//   only N       light just pixel N-1 (probe a specific position / far end)
//   chase [S [MS]]  walk a single pixel from index S (default 0) to NUM_LEDS-1,
//                MS ms/step (default 50). With S>0 every index is printed, so
//                e.g. "chase 280 400" shows exactly where the light vanishes.
//   color r|g|b|w|y|c|m   set color for run/only (default w)
//   bright B     set brightness, clamped to 8..64
//   off          all dark
//   status       print current settings
//
// Protan note: r/g are easy to confuse by eye. Prefer w (white), b (blue) and
// positions/counts to judge results.

#include <FastLED.h>

#define DATA_PIN 27
#define NUM_LEDS 300        // upper bound until A4 gives the real count
#define COLOR_ORDER GRB     // set to match A1's result
#define MIN_BRIGHT 8
#define MAX_BRIGHT 64       // bench cap: 12V/2A adapter, strip unrolled, no heatsink

CRGB leds[NUM_LEDS];
CRGB current = CRGB::White;
uint8_t bright = 32;
int litCount = 0;

void clearAll() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  litCount = 0;
}

void doRun(int n) {
  n = constrain(n, 0, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < n; i++) leds[i] = current;
  FastLED.show();
  litCount = n;
  Serial.printf("lit pixels 0..%d (%d pixels)\n", n - 1, n);
}

void doOnly(int n) {
  n = constrain(n, 1, NUM_LEDS);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[n - 1] = current;
  FastLED.show();
  litCount = 1;
  Serial.printf("lit only pixel index %d (position %d)\n", n - 1, n);
}

void doChase(int start, int ms) {
  start = constrain(start, 0, NUM_LEDS - 1);
  ms = constrain(ms, 10, 2000);
  Serial.println("chase: watch where the pixel disappears");
  for (int i = start; i < NUM_LEDS; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    leds[i] = current;
    FastLED.show();
    if (start > 0 || i % 25 == 0) Serial.printf("at index %d (position %d)\n", i, i + 1);
    delay(ms);
  }
  clearAll();
  Serial.println("chase done");
}

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

void handle(String line) {
  line.trim();
  line.toLowerCase();
  if (line.length() == 0) return;
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);
  arg.trim();

  if (cmd == "run")         doRun(arg.toInt());
  else if (cmd == "only")   doOnly(arg.toInt());
  else if (cmd == "chase")  {
    int sp2 = arg.indexOf(' ');
    int st = arg.length() ? arg.toInt() : 0;
    int ms = sp2 < 0 ? 50 : arg.substring(sp2 + 1).toInt();
    doChase(st, ms);
  }
  else if (cmd == "off")    { clearAll(); Serial.println("off"); }
  else if (cmd == "color")  {
    if (arg.length() && setColor(arg[0])) Serial.printf("color %c\n", arg[0]);
    else Serial.println("color: r g b w y c m");
  }
  else if (cmd == "bright") {
    bright = constrain(arg.toInt(), MIN_BRIGHT, MAX_BRIGHT);
    FastLED.setBrightness(bright);
    FastLED.show();
    Serial.printf("brightness %d (cap %d)\n", bright, MAX_BRIGHT);
  }
  else if (cmd == "status") {
    Serial.printf("brightness %d, lit %d, color %d,%d,%d, NUM_LEDS %d\n",
                  bright, litCount, current.r, current.g, current.b, NUM_LEDS);
  }
  else Serial.println("commands: run N | only N | chase [S [MS]] | color X | bright B | off | status");
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(bright);
  clearAll();
  Serial.println("multipixel ready. try: run 10");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handle(line);
  }
}
