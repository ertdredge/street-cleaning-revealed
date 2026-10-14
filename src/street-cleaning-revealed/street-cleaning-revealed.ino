// A1: color-order check. Lights pixel 0 only, cycling commanded RED, GREEN, BLUE.
// Compare what you SEE to what the serial monitor says was COMMANDED.
//
// Sketch is set to GRB. Observed sequence (commanded R, G, B) -> strip's real order:
//   red,   green, blue   -> GRB  (nothing to change)
//   green, red,   blue   -> RGB
//   red,   blue,  green  -> BRG
//   blue,  green, red    -> GBR
//   blue,  red,   green  -> RBG
//   green, blue,  red    -> BGR
// Then replace GRB in addLeds<> with the real order and re-run to confirm.

#include <FastLED.h>

#define DATA_PIN 27
#define NUM_LEDS 300        // placeholder until A4 (count); extra data is harmless
#define BRIGHTNESS 32
#define COLOR_ORDER GRB     // <-- change this after reading the table above

CRGB leds[NUM_LEDS];

void showOne(const char *name, CRGB color) {
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[0] = color;
  FastLED.show();
  Serial.print("commanded: ");
  Serial.println(name);
  delay(3000);
  fill_solid(leds, NUM_LEDS, CRGB::Black);   // dark gap marks the boundary
  FastLED.show();
  delay(1500);
}

void setup() {
  Serial.begin(115200);
  FastLED.addLeds<WS2812B, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
}

void loop() {
  Serial.println("--- cycle start ---");
  showOne("RED",   CRGB::Red);
  showOne("GREEN", CRGB::Green);
  showOne("BLUE",  CRGB::Blue);
}
