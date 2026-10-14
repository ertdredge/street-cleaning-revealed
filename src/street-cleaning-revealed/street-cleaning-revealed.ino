// A3 (two-run version): multiple pixels on TWO outputs. Serial-driven
// (115200 baud, newline-terminated).
//
// Hardware: two independent 150-pixel runs, both fed from the same 12 V supply.
//   Run A: GPIO 27 -> 74AHCT125N gate 1 -> 470 ohm -> left strip (90) -> top strip (60)
//   Run B: GPIO 33 -> 74AHCT125N gate 2 -> 470 ohm -> bottom strip (60) -> right strip (90)
// One 300-element array: indexes 0..149 are run A, 150..299 are run B.
//
// For now every command does the SAME thing on both runs. Pixel numbers below
// are positions within a run (1..150), applied to run A and run B alike.
//
// Commands:
//   run N        light pixels 0..N-1 on each run (2*N pixels total), current color
//   only N       light just the Nth pixel (1..150) of each run
//   chase [S [MS]]  walk a single pixel along each run from index S (default 0) to
//                149, MS ms/step (default 50). With S>0 every index is printed.
//   color r|g|b|w|y|c|m   set color for run/only (default w)
//   bright B     set brightness, clamped to 8..48
//   off          all dark
//   status       print current settings
//
// Protan note: r/g are easy to confuse by eye. Prefer w (white), b (blue) and
// positions/counts to judge results.

#include <FastLED.h>

#define DATA_PIN_A 27       // run A: left (90) -> top (60)
#define DATA_PIN_B 33       // run B: bottom (60) -> right (90); silkscreen "33"
#define RUN_LEN 150
#define NUM_LEDS (2 * RUN_LEN)
#define COLOR_ORDER GRB     // verified in A1
#define MIN_BRIGHT 8
#define MAX_BRIGHT 48       // bench cap: all 300 pixels white at 64 would be ~1.9 A, too close to the 2 A adapter

CRGB leds[NUM_LEDS];
CRGB current = CRGB::White;
uint8_t bright = 32;
int litCount = 0;           // lit pixels per run

void setBoth(int i, CRGB c) {
  leds[i] = c;               // run A
  leds[RUN_LEN + i] = c;     // run B
}

void clearAll() {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
  litCount = 0;
}

void doRun(int n) {
  n = constrain(n, 0, RUN_LEN);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  for (int i = 0; i < n; i++) setBoth(i, current);
  FastLED.show();
  litCount = n;
  Serial.printf("lit pixels 0..%d on each run (%d per run, %d total)\n", n - 1, n, 2 * n);
}

void doOnly(int n) {
  n = constrain(n, 1, RUN_LEN);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  setBoth(n - 1, current);
  FastLED.show();
  litCount = 1;
  Serial.printf("lit only pixel index %d (position %d) on each run\n", n - 1, n);
}

void doChase(int start, int ms) {
  start = constrain(start, 0, RUN_LEN - 1);
  ms = constrain(ms, 10, 2000);
  Serial.println("chase: watch where the pixel disappears on each run");
  for (int i = start; i < RUN_LEN; i++) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    setBoth(i, current);
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
    Serial.printf("brightness %d, lit %d per run (%d total), color %d,%d,%d, runs 2 x %d, pins %d and %d\n",
                  bright, litCount, 2 * litCount, current.r, current.g, current.b,
                  RUN_LEN, DATA_PIN_A, DATA_PIN_B);
  }
  else Serial.println("commands: run N | only N | chase [S [MS]] | color X | bright B | off | status");
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN_A, COLOR_ORDER>(leds, 0, RUN_LEN);
  FastLED.addLeds<WS2812B, DATA_PIN_B, COLOR_ORDER>(leds, RUN_LEN, RUN_LEN);
  FastLED.setBrightness(bright);
  clearAll();
  Serial.println("multipixel (2 x 150) ready. try: run 10");
}

void loop() {
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    handle(line);
  }
}
