// Hello World illuminate a single pixel

#include <FastLED.h>
#define DATA_PIN 27
#define NUM_LEDS 300   // upper bound; first light touches only LED 0
CRGB leds[NUM_LEDS];
void setup() {
  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(32);
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  leds[0] = CRGB::White;   // white is white in any color order
  FastLED.show();
}
void loop() {}
