# Street Cleaning ReveaLED — Project Plan

---

## 1. Purpose and one-paragraph summary

Outdoor LED art installation that frames a window of roughly **38.5" x 60"*. The bottom strip animates to urge parallel parked cars to the safe side of the street when street cleaning is imminent, and changes color based on trash collection or yard waste collection. The top strip shows the weather forecast: a bar from the window's center for how the day's forecast high compares with typical, a dark notch on the same scale for tonight's forecast low, a breathing animation when precipitation is forecast and a sparkle when snow is. The **two side strips act as bar graphs** whose displayed value ranges over **0–90** and is **updated hourly**, with colors and values that reflect how risky it is to park on that side of the street. Control is by an **ESP32 microcontroller running FastLED**. The controller uses **WiFi and HTTPS** to read the National Weather Service forecast, which drives the top strip and the forecast snow risk on the side bars, and the City of Somerville's news feed, which reports parking snow emergencies the city actually declares.

---

## 2. Requirements and constraints

### 2.1 Functional

| Requirement | Detail |
|---|---|
| Geometry | Frame a window ~105 cm wide × ~160 cm tall. Perimeter ≈ 2 × (105 + 160) = **530 cm**. A single ~5 m reel is "slightly too short" for the full 530 cm |
| Bottom | Animate for parking and color for waste collection. |
| Top | Weather forecast for today (tomorrow's from 5 pm): on a pale green-white background, a bar from the center, scarlet to the right for a high above typical or sky blue to the left for one below; a two-pixel dark notch on the same scale for tonight's low (rolls over at 6 am); breathing when precipitation is forecast; sparkle when snow is. See `Ambient-Display-Encoding.md` section 3. |
| Sides | Bar graphs, values 0–90, updated hourly. |
| Firmware | FastLED on ESP32. |
| Connectivity | WiFi + HTTPS to fetch the NWS gridpoint forecast hourly (design: `weather-feed.md`) and the city's news RSS feed hourly for declared parking snow emergencies (design: `snow-emergency-feed.md`). |

### 2.2 Visibility

- **Nice-to-have:** visible from 150 m at dusk.
- **Wanted:** visible from 20 m in non-direct daylight.
- **Not required:** visibility in direct sun.

These were deliberately relaxed from an early "visible at 150 m in daylight" idea, which would have driven a much larger and costlier build.

### 2.3 Environment

- Outdoors, year-round (large temperature swings, rain, snow, freeze–thaw).
- The IP68 strip's own listing gives an operating range of **−20 °C to +40 °C**. Connection points still need protection from direct water.

---

## 3. Display overview

The project will illustrate the number of days remaining to a biweekly street cleaning event on the two sides of a street.  One side is second and fourth Fridays, the other side is first and third Thursdays.  Number of days until the next street sweeping event will be calculated and used as the basis for which LEDs are illuminated.

Trash and yard waste collection is every Wednesday on this street unless delayed to Thursday by a holliday.

Snow emergencies are called by the city periodically and parking is prohibited on one side of the street or the other. Until the city declares one, the side bars use the NWS snow forecast (Phase W, W6). Declared emergencies are read from the city's news RSS feed: the parser and tracker are written and tested, and wiring them into the display is still to do (Phase S; design in `snow-emergency-feed.md`).

The top strip shows the weather forecast instead of repeating the bottom strip. Encoding rules are in `Ambient-Display-Encoding.md` section 3; the forecast source, derived values and failure policy are in `weather-feed.md`. A fetch failure or a forecast older than 12 hours leaves the top strip completely dark.

### 3.1 Installation layout

- **Strip pieces, all cut from the one 300-pixel reel:** left side 90 pixels (about 1.5 m), right side 90 pixels (about 1.5 m), top 60 pixels (about 1.0 m), bottom 60 pixels (about 1.0 m). 90 + 90 + 60 + 60 = 300, so there is **no spare pixel**, and cuts must land exactly on the marked cut pads. The pieces are slightly shorter than the 160 cm / 105 cm window edges.
- **Four separate strips**, joined at the corners by waterproof pigtail connectors.
- **Two independent 150-pixel runs, both starting at the lower-right corner** (changed from the lower-left on 2026-11-05; the sketch is already updated):

| Run | Data | Strips in order | Logical indexes |
|---|---|---|---|
| A | GPIO27 → 74AHCT125 gate 1 | bottom (right to left), then left (bottom to top) | 0–59 bottom, 60–149 left |
| B | GPIO33 → 74AHCT125 gate 2 | right (bottom to top), then top (right to left) | 150–239 right, 240–299 top |

  Both side bars fill upward as the index increases; the top and bottom strips are addressed right to left, against their display direction, which the sketch's `Strip` table handles with a `reversed` flag (logical position 0 is always the left end). The upper-left corner has no joint: run A's last pixel (top of the left strip) and run B's last pixel (left end of the top strip) are both sealed dead ends there. Which run goes on which pin is a wiring choice; if you swap them, swap `DATA_PIN_A` and `DATA_PIN_B` and the strip table's `first` values (bottom and left start at 150, right and top at 0).
- **Electronics are stored indoors** (Feather, level shifter, 12 V supply). **Two cables** run from the electronics through one hole drilled in the window casing at the lower-right corner, one per run, to the head of run A (right end of the bottom strip) and the head of run B (bottom of the right strip). Each cable carries +12 V, GND, DATA, and BI (tied to GND at the head).
- **Why two runs rather than one 300-pixel daisy chain:** voltage drop along a run scales roughly with the square of its length, so 150-pixel runs should show about a quarter of the drop of a single 300-pixel feed; each pigtail and joint carries at most half the current; a failed corner joint loses at most 60 or 90 pixels instead of 210; and it costs only a spare gate on the already-purchased 74AHCT125 plus one more GPIO. Rejected: a single pigtail feeding a 300-pixel chain around all four sides.
- **Firmware shape:** one 300-element array, two controllers, so the bar-graph logic can address a logical array:

```cpp
FastLED.addLeds<WS2812B, 27, GRB>(leds, 0, 150);       // run A: bottom + left
FastLED.addLeds<WS2812B, 33, GRB>(leds, 150, 150);     // run B: right + top
```

- **Fusing:** fuse each 12 V feed separately (about 2–3 A).

---

### 3.2 Final deployment hardware (decided 2026-11-07): QuinLED Dig-Next-2 and Mean Well GST90A12-P1M

This replaces the Feather V2, the 74AHCT125N level shifter, the 470 Ω resistors and the 12 V bench adapter of the bench build (sections 4 to 6 describe the bench). Facts from QuinLED's pages ([pinout guide](https://quinled.info/dig-next-2-pinout-guide/), [power handling](https://quinled.info/dig-next-2-power-handling/), [specifications](https://quinled.info/dig-next-2-specifications/), [relays](https://quinled.info/dig-next-2-internal-external-relays/)) and the Mean Well GST90A datasheet:

| Item | Value |
|---|---|
| Data, run A | LED1 = GPIO 2 (level-shifted on the board, 33 Ω series resistor on the board) |
| Data, run B | LED2 = GPIO 4 |
| Power outputs | 5A_1, 10A_1, 5A_2: each fused, each switched on the + side by an on-board MOSFET "relay". Run A and run B use 5A_1 and 5A_2; 10A_1 is unused |
| Relay pins | 5A_1 = GPIO 20, 10A_1 = GPIO 21, 5A_2 = GPIO 22 (the polarity and boot state are not documented; the sketch assumes HIGH = on: verify) |
| Other pins | external AC relay trigger GPIO 5 (5 V), buttons GPIO 34/35 (input only), I2C SDA 15 / SCL 14, QEXT GPIO 0, 25, 32, 33, microphone GPIO 7/8. All GPIO are 3.3 V only |
| Input | 5 to 48 V on the orange 6.35 mm terminal (the board passes the input voltage straight to the outputs); USB-C 5 V powers only the controller, never the LEDs |
| Supply | Mean Well GST90A12-P1M: 12 V, 6.67 A, 80 W; output plug 2.5 × 5.5 mm (not 2.1), centre positive, 1 m of 14 AWG cable; IEC C14 inlet, Class I (earthed); hiccup-mode overload protection; inrush up to 70 A at 230 V |

The wiring schematic is `hardware/schematics/dig-next-2-deploy.svg` (the bench schematic `street-cleaning-revealed.svg` is kept for reference).

Sketch constants: `DATA_PIN_A`, `DATA_PIN_B`, `RELAY_PIN_5A_1`, `RELAY_PIN_5A_2`, `RELAY_ON_LEVEL`. Switch back to the Feather by restoring 27 and 33 and setting both relay pins to -1. The `relay [on|off]` serial command shows or changes the strip power. The 5 V rail current limit that matters is the 5 A fuse per run (150 pixels is about 3.8 A at full white and brightness 255).

## 4. System architecture

### 4.1 Block diagram (bench configuration, two runs)

```
  Laptop USB-C ──► ESP32 Feather V2 (3.3 V logic)
                      │  USB pin (≈5 V) ───────────────► 74AHCT125 pin 14 (VCC)
                      │  GND ──────────────────────────► breadboard − rail ──► 74AHCT125 pin 7
                      │  GPIO27 ───────────────────────► 74AHCT125 pin 2 (1A)
                      │  GPIO33 ───────────────────────► 74AHCT125 pin 5 (2A)
                                                         74AHCT125 pin 3 (1Y) ─► 470 Ω ─► run A DI
                                                         74AHCT125 pin 6 (2Y) ─► 470 Ω ─► run B DI

  12 V wall adapter (barrel → screw-terminal adapter)
      + screw ─► run A VCC + run B VCC + 1000 µF capacitor (+ leg)
      − screw ─► run A GND + run B GND + capacitor stripe (− leg) ──► jumper to breadboard − rail
                                                         run A BI, run B BI ─► breadboard − rail (GND)
```

The current schematic is `hardware/schematics/street-cleaning-revealed.svg`.

Key principles:

1. **The strips and the Feather are separate power domains with a shared ground.** The 12 V supply powers only the strips. The Feather and the level-shifter chip are powered from USB (5 V).
2. **The 12 V never passes through the breadboard.** Breadboard carries only logic, 5 V for the chip, and ground.
3. **Level shifting:** the ESP32 outputs 3.3 V logic, while WS2815 expects ~5 V-class logic referenced to a 12 V strip. A 74AHCT125 powered at 5 V shifts the signal up and buffers it.

### 4.2 Why these parts (rationale)

- **WS2815 (12 V, backup data line):** 12 V roughly halves the current for the same power compared with 5 V strips, which lowers voltage drop on long runs. The backup line (BI) lets the chain keep working if a single pixel dies. It speaks the same single-wire protocol as WS2812B.
- **ESP32 Feather V2:** WiFi built in, 8 MB flash, 2 MB PSRAM, USB-C, comfortable FastLED support, and the Feather form factor allows a terminal-block FeatherWing for a solder-free permanent install.
- **74AHCT125N level shifter (DIP-14):** through-hole, breadboard-friendly, inexpensive, and its four gates cover both data lines with two to spare. The "T" in AHCT means its inputs accept TTL-level (3.3 V) signals while running at 5 V.
- **470 Ω series resistor on each data line:** damps ringing and protects the first pixel.
- **1000 µF, 25 V electrolytic across the strips' 12 V feed:** absorbs inrush. It **must be rated well above 12 V**; an early candidate (4700 µF, 10 V) was correctly rejected as under-rated.
- **0.1 µF ceramic bypass across chip pins 14 and 7:** local supply decoupling. Ceramic, so **no polarity**.

---

## 5. Bill of materials

### 5.1 Digi-Key

| Part | Role |
|---|---|
| Adafruit ESP32 Feather V2 (Digi-Key listing for Adafruit 5900) | Controller; the "with headers" variant keeps the build solder-free |
| SN74AHCT125N (2×) | Level shifter (one spare) |
| 1000 µF, 25 V radial electrolytic (2×) | Strip power-input capacitor (one spare) |
| C320C104K5R5TA, 0.1 µF ceramic radial (3×) | Chip bypass capacitors |
| Mean Well LPV-150-12 | **Deployment** 12 V supply |

### 5.2 Amazon

| Part | Role |
|---|---|
| BTF-LIGHTING WS2815 strip, **IP68**, with weatherproof pigtail connector | The LED strip (one 300-pixel reel, cut into four pieces) |
| BTF IP65 4-pin extension cables | Extension between connectors / to the controller area |
| WESIRI JST pigtails | Pigtail leads for connection |
| 12 V, 2 A wall adapter with barrel-to-screw-terminal adapter | **Bench** power (24 W) |
| Resistor assortment kit | Source of the 470 Ω resistor |
| HUAREW breadboard kit (breadboard, jumpers, power module) | Bench assembly |

### 5.3 Deferred / deployment parts (not yet bought)

- 12 V → 5 V buck converter (e.g., PlusRoc USB-C type) to feed the Feather/shifter from the 12 V supply.
- Adafruit Terminal Block Breakout FeatherWing (assembled), so pins are clamped by screws rather than friction fit.
- ~~Weatherproof enclosure(s)~~ Not needed if electronics remain indoors. Still needed: sealant and strain relief where the two cables pass through the window casing, drip loops, and rated connectors or sealing for the cut strip ends.
- Mains-side wiring solution for the LPV-150-12's bare AC leads (see D6 in §7).
- Mounting: **plastic** channel/clips or isolating tape (see D3 in §7).
- Fuse(s) on the 12 V feed.

---

## 6. Pin map and wiring detail (as built and verified on the bench)

### 6.1 74AHCT125N (DIP-14)

Orientation: notch **up**; pin 1 at top-left; pins 1–7 run down the left side; pin 8 at bottom-right; pins 8–14 run up the right side. So pin 14 (VCC) is top-right next to the notch, and pin 7 (GND) is bottom-left.

| Pin | Name | Connection |
|---|---|---|
| 1 | 1OE (active-low) | **GND** (essential: floating or high disables the gate) |
| 2 | 1A | Feather GPIO27 |
| 3 | 1Y | → 470 Ω → strip DI (green) |
| 4 | 2OE | GND |
| 5 | 2A | GND (unused input, don't float) |
| 6 | 2Y | not connected |
| 7 | GND | Ground rail |
| 8 | 3Y | not connected |
| 9 | 3A | GND |
| 10 | 3OE | GND |
| 11 | 4Y | not connected |
| 12 | 4A | GND |
| 13 | 4OE | GND |
| 14 | VCC | Feather **USB** pin (≈5 V); 0.1 µF ceramic from pin 14 to pin 7 |

**Run B wiring (gate 2):** pin 4 (2OE) stays on GND; pin 5 (2A) goes to Feather GPIO33 instead of GND; pin 6 (2Y) goes through a second 470 Ω resistor to run B's DI. Gates 3 and 4 stay tied low. The table above is the original single-run wiring.

### 6.2 Strip wires (input end)

| Wire (observed) | Role | Connection |
|---|---|---|
| Yellow | GND | Adapter **−** screw (with capacitor stripe/− leg) and jumper to breadboard − rail |
| Blue | BACKUP (BI) | Breadboard − rail (GND) |
| Green | DATA (DI) | Via 470 Ω from chip pin 3 |
| Red | VCC, 12 V | Adapter **+** screw (with capacitor + leg) |

BTF's packaging lists VCC red, GND white, DATA green, BACKUP blue. The pigtail has **yellow where the packaging lists white**. Roles were checked electrically on the bench.

### 6.3 Feather ESP32 V2 facts

- Chip: ESP32-PICO-V3-02, 8 MB flash, 2 MB PSRAM, USB-C with CH9102F USB-serial.
- Data pins: **GPIO27** for run A, silkscreen "27" (Adafruit's pinout page lists it as D27, with an analog alias), and **GPIO33** for run B (the pinout page lists it as SS / A9). The sketch defines `DATA_PIN_A` as 27 and `DATA_PIN_B` as 33.
- Onboard red LED: `LED_BUILTIN` (pin 13).
- The "USB" pin is the 5 V rail from the USB connector; the board's logic is 3.3 V.

---

## 7. Rough Project plan

### Phase A — Bench step 2: color and programmatic control

Goals: confirm color order, control multiple pixels, and prototype the bar-graph logic, all on the uncut strip, unrolled.

1. ✅ **A1. Color-order check — COMPLETE (2026-10-11).** Light pixel 0 red, then green, then blue (separately). If red shows as green, correct the order from `GRB` to the right permutation.
2. ✅ **A2. Unroll and lay out — COMPLETE (2026-10-11).** Lay the strip out flat (or in wide, open serpentine runs with spacing). Keep brightness 32–64.
3. ✅ **A3. Multiple pixels — COMPLETE (2026-10-11).** Light a run of N pixels; verify addressing along the strip and that the far end is reachable. Measure the supply voltage at the strip input under a representative load.
4. ✅ **A4. Count and density — COMPLETE (2026-10-11).** Determine the actual LED count and density (LEDs per meter) from the order and by counting; replace the placeholder `NUM_LEDS 300`.
5. ✅ **A5. Bar-graph function.** Write `show_bar(value, color)` for 0–21, deciding how 21 levels map onto a 90-pixel side strip (about 4.3 pixels per step; pixels per step, partial steps, "off" color). Handle clamping and color changes. Test on a section of the uncut strip.
6. ✅ **A6. Input via serial.** Accept a value from the serial monitor (or a shell/Ruby script over serial) so the bar can be changed without reflashing.
7. ✅ **A7. Power check.** Estimate worst-case current for the chosen color scheme and confirm it fits the 2 A bench adapter.

*Exit criteria:* correct colors; a bar that fills 0–21 predictably; no heat or voltage problems at the chosen brightness.

### Phase B — Bench step 3: cut into four pieces; two independent 150-pixel runs

Layout and rationale in §3.1.

1. ✅ **B1. Cut.** Cut the reel at the marked pads into 90 (left), 90 (right), 60 (top), 60 (bottom). Zero spare pixels, so mark and check each cut before making it.
2. ✅ **B2. Terminate and seal.** Six four-wire terminations are needed on cut ends: pigtail A to the bottom strip's DI end (its right end); the bottom strip's DO end (left end); the left strip's DI end (bottom); pigtail B to the right strip's DI end (bottom); the right strip's DO end (top); the top strip's DI end (right end). Two dead ends need sealing: the left strip's DO end and the top strip's DO end (left end). (Layout as of 2026-11-05, with the cables entering at the lower-right corner.) The reel's two factory connectors can absorb up to two of these if they mate with the purchased BTF extension cables. Solder versus snap-on connectors is still open.
3. ✅ **B3. Wire run B's data path.** Gate 2 of the 74AHCT125 and a second 470 Ω resistor, per §6.1. Run B uses GPIO33.
4. ✅ **B4. Update the sketch.** One 300-element array, two `addLeds` calls (see §3.1).
5. ✅ **B5. Test.** Each run individually, then together. Verify the index map (0–59 bottom right to left, 60–149 left bottom to top, 150–239 right bottom to top, 240–299 top right to left). Confirm each head's BI is on GND. Repeat the full-length voltage measurement per run at the real feed.

Considerations:

- **Cut only at marked cut points** (copper pads); corners are joins between separate strips rather than 90° bends in a strip.
- The IP68 strip is silicone-sealed; **cut ends must be re-sealed** and need connectors (or soldering). The "no solder" goal is unlikely to hold fully here.
- **Each run head needs** BI to GND and DI fed with data from its own gate.
- **Power:** each run gets its own parallel feed from the 12 V supply, not series.
- Retest each piece individually before testing together.

### Phase C — Network and weather feed

1. ✅ **C1. WiFi bring-up.** Connect the ESP32 to WiFi.
2. ✅ **C2. Time.** Use NTP so the schedules are meaningful; handle timezone (America/New_York) and DST.
3. **C3. HTTPS fetch.** The NWS gridpoint feed (`https://api.weather.gov/gridpoints/BOX/70,102`) is fetched by `weather-feed.cpp` and parsed by `weather-forecast.cpp` (design in `weather-feed.md`). The fetch currently skips certificate verification (`setInsecure()`), which is acceptable for public read-only data driving an art installation but is not the plan's original goal; pinning a root certificate is supported by `HttpsForecastSource::open()` and remains a decision. TLS roots expire, so a pinned certificate needs a renewal plan.
4. ✅ **C4. Forecast parsing.** Forecast high, probability of precipitation and snow values per local day (the overnight low is added in Phase T, T3), streamed through an ArduinoJson filter (about 30 KB of heap, never the 290 KB response). Replaces the original "keyword parse" idea. Host-tested against real captures and synthetic variants (`test/`).
5. ✅ **C5. Polling.** A FreeRTOS task on core 0 (20 KB stack) fetches hourly once WiFi is up and the clock is set, and retries after 5 minutes on failure. The renderer on core 1 only reads the latest snapshot, under a mutex. A failure never discards good data, and the snapshot expires after 12 hours.
6. **C6. Persistence.** Store the last good forecast in NVS flash so that a reboot does not leave the top strip dark until the first fetch. Not started; optional.
7. **C7. Observability.** Serial commands today: `forecast` prints the snapshot age, each day's values and each night's low; `low C|off` overrides the night's low, and `precip P|off` overrides the precipitation probability for testing. A status LED pattern or a trivial web endpoint is not started.

### Phase W — Top strip weather display

Encoding: `Ambient-Display-Encoding.md` section 3.

1. ✅ **W1. Default display (3.1).** Center 20 pixels spring green (0, 72, 36); the cold and hot groups dark. *Superseded by Phase T: all 60 pixels lit in a pale green-white background.*
2. ✅ **W2. Forecast precipitation (3.3).** All lit pixels breathe at a rate proportional to the probability of precipitation: 0.5 Hz (2 s period) at 100%, none below 10% (`PRECIP_MIN_PERCENT`; NWS reports 2% for "not expected"), brightness between 15% and full. `BREATH_MAX_HZ` and `BREATH_MIN_LEVEL` are constants in the sketch.
3. ✅ **W3. No-data state.** The top strip is completely dark when there is no forecast or it is stale.
4. ✅ **W4. Deviations from typical (3.2).** The forecast high for the displayed day is compared with `docs/typical-high-temperatures.csv` (366 rows including February 29, compiled into the sketch by `bin/make-typical-highs`). Scarlett grows into the hot group (one pixel per half degree) and bleeds into the center (10% per pixel); sky blue mirrors it on the cold side. Pure logic and host tests: `weather-display.h/.cpp`, `test/weather-display-test.cpp`. Serial: `high C|off` overrides the forecast high. *Superseded by Phase T: a single bar from the center of the strip, two pixels per °C, with no bleed.*
5. ✅ **W5. Snow or sleet (3.3.1).** Random pixels of the top strip flash white (and fade over `SPARKLE_SECONDS`, `SPARKLE_PER_SEC` new flashes a second) when snow is forecast for the displayed day. The feed carries snow accumulation only, so sleet is not distinguished.
6. ✅ **W6. Snow removal risk (1.2), NWS data only.** The snow forecasts for today and the next three days (at least 72 hours ahead) are examined as separate snow events (a storm split across midnight is rejoined), and the riskiest event counts; the street cleaning risk function is evaluated for the hours until the snow starts, then scaled by inches / 4 (under 4 in) and percent / 100. Only the bar for the side where snow-emergency parking is banned (right in winter 2026/27, left in 2027/28, alternating from July 1) shows the larger of that and its street cleaning risk; above the march threshold (55) the bottom strip also sparkles, and the whole snow bar sparkles whenever the snow risk exceeds its street cleaning risk. City-declared snow emergencies are added in Phase S. Pure logic and host tests: `snow-risk.h/.cpp`, `test/snow-risk-test.cpp`. Serial: `snow P IN H|off` overrides the snow forecast.

### Phase S — City-declared snow emergencies

Design: `snow-emergency-feed.md`. Display rule: `Ambient-Display-Encoding.md` 1.2 ("If the city declares an actual snow emergency then the start of the parking ban is used to calculate $remaining$").

1. ✅ **S1. Source selection (2026-10-30).** The city publishes no machine-readable status, and the Everbridge API and CAP feed are available only to the sending organization. The city's news RSS feed (`https://www.somervillema.gov/news.xml`) is the sole source, parsed on the ESP32 with no server-side relay. Coverage check against the City Alerts emails, 2024–2026: four of ten alerts had no news item, all before February 9, 2025; all five since then had one.
2. ✅ **S2. Headline parser, RSS scanner and tracker (2026-10-30).** `snow-emergency.h/.cpp`, plain C++ with no Arduino headers. The parser recognizes declaration and end headlines and extracts the time, date and (inferred) year. The scanner streams the feed (about 7 KB kept, never the body). The tracker keeps the start and end of the parking ban, survives missed and repeated items, and drops an emergency with no announced end after 96 hours. Host tests: `test/snow-emergency-test.cpp` (119 checks, all real 2024–2026 headlines), fixtures in `test/data/snow-emergency/`.
3. ✅ **S3. Fetch layer.** `HttpsForecastSource::open()` takes an optional `accept` header (default unchanged, `application/geo+json`), so the same class fetches the RSS feed.
4. ✅ **S4. Live check (2026-10-30).** `curl -s https://www.somervillema.gov/news.xml | ./snow-emergency-test -` read all 10 live items, with no notices.
5. ✅ **S5. Wire into `street-cleaning-revealed.ino` (2026-10-30; not yet compiled for the ESP32).** Outline in `weather-feed.md` section 9.3 and `snow-emergency-feed.md` section 6.3. Done:
   - Hourly fetch on core 0 after WiFi and the clock, one TLS session at a time (in the weather task or its own task). `RssScanner` static, not on the task stack. Keep the state on a failed fetch or a body that is not a feed; retry sooner.
   - The tracker shared with the renderer under a mutex.
   - `state.start` and `state.end` saved to NVS (`Preferences`) when they change and restored in `setup()`. Without this a reboot during an emergency loses it once the declaration has left the 10-item feed.
   - In the snow removal risk, while the phase is not none, a full-size snow event (`SNOW_EMERGENCY_INCHES`, 100%) starting at `state.start`. Left out while the clock is simulated.
   - Decided: once the announced end has passed, forecast snow events that started before it are dropped (same storm); a forecast snowfall that starts later still counts.
   - Serial: `emergency` (city feed state, override, phase, last poll), `emergency start|stop WHEN`, `emergency off`. Test-suite cases: declared, in effect, ended.
   - The poll runs in `weatherTask` after the forecast fetch (one TLS session at a time) and is wrapped in `DechunkingSource` in case the city's server chunks the response.
   - Still to do: the first compile and upload for the ESP32 (`snow-emergency.cpp` and the new sketch code are so far host-compiled and syntax-checked against stubs only).
6. ✅ **S6. Documentation after wiring.** Updated `Ambient-Display-Encoding.md` 1.2.1 ("Only the NWS forecast is used…") and the Phase W, W6 note.
7. **S7. Explainer headlines (optional).** Treat a headline with `?`, or with *when / if / what / how / why / before / after* just before "snow emergency", as not a notice (`snow-emergency-feed.md` 4.6). Harmless today: untimed declarations are ignored.
8. **S8. Real fixture.** Capture the raw feed during the first emergency of winter 2026/27 and add it to `test/data/snow-emergency/`.

*Exit criteria:* a declared emergency moves the snow bar on schedule from the feed alone, survives a reboot, and clears at its announced end (or 96 hours after its start).

### Phase T — Top strip redesign: center bar and night notch

Encoding: `Ambient-Display-Encoding.md` section 3 (rewritten 2026-11-11). Feed: `weather-feed.md` 1, 3.1, 3.2, 4.7 and 5.3. Replaces the three-group display of W1 and W4; breathing (W2), the no-data state (W3) and the snow sparkle (W5) are unchanged.

1. ✅ **T1. Design (2026-11-11).** One 60-pixel scale with "typical" at the window's center, two pixels per °C (about one per °F). The high is a solid bar from the center, scarlet to the right or sky blue to the left; tonight's low is a two-pixel dark notch on the same scale. The background is a pale green-tinted off-white (35, 43, 38) at 75% of the bar colors' luminance, so warm and cold are read from position and brightness, not hue alone. The notch rolls over at 6 am, separately from the 5 pm rollover of the high, precipitation and snow. Considered and not used: bars growing in from the ends on a fully lit spring green field (scarlet on spring green at equal luminance nearly vanishes for red–green color-blind viewers); "frost and glow" split halves (low on the left half, high on the right); a bright bead on an always-lit blue-to-scarlet gradient.
2. ✅ **T2. Typical lows.** Done: `bin/make-typical-highs` now generates both `typical-highs.cpp` and `typical-lows.cpp` (`TYPICAL_LOW_C[366]`) in one run (same name and usage, so the cheat-sheet is unchanged), and `typicalLowC(month, mday)` sits beside `typicalHighC()` with the same NAN rule; tests in `test/weather-display-test.cpp`. Original brief: use the newly added `docs/typical-low-temperatures.csv`: 366 rows including February 29, in the format of `typical-high-temperatures.csv` (`Month/Day,Typical Low`) and from the same kind of source (e.g. the NOAA 1991–2020 daily normal minimum for Boston) to extend `bin/make-typical-highs` to also generate `TYPICAL_LOW_C[366]` (e.g. into `typical-lows.cpp`) and add `typicalLowC(month, mday)` beside `typicalHighC()` in `weather-display.h/.cpp`. Update the cheat-sheet (§8) if the generator's name or usage changes.
3. ✅ **T3. Forecast low in the parser** (done: `nightLow(night, now)` with `NIGHT_LAST`/`NIGHT_TONIGHT`..., `LowOutlook`; the snapshot holds the nights beginning yesterday through day 4; fixtures `no-lows`, `lows-bad-unit`, `lows-fahrenheit`; `weather-cli` prints the nights; peak parser heap 29.6 KB → 30.0 KB on a 64-bit host, and the independent cross-check matched the real capture's five nights; the `NIGHT_LAST` low is unknown in that capture because only the cut remainder exists) (`weather-forecast.h/.cpp`; design `weather-feed.md` 4.7).
   - Add `minTemperature` to the ArduinoJson filter as an **optional** series: not in `NEEDED`, so a feed without it (or with an unrecognised unit for it) still parses and reports no lows. Same unit handling as `maxTemperature`.
   - Lows per night, named by the local date the night begins: the interval that starts in that local day at or after local noon. Ignore an interval starting before noon (the previous night's remainder, cut at the forecast cycle start). Carry the last-seen value forward for the same night.
   - Keep the night that began yesterday in the snapshot (shift it at the local-day change rather than discard it): it is shown from midnight until 6 am.
   - A new accessor returning one night's low (e.g. `nightLow(night, now)` with yesterday's and today's nights at least), valid flag and `NAN` when unknown; the 12-hour expiry applies.
   - Re-measure the parser's heap (about 30 KB today); the filter keeps about seven more intervals.
   - Host tests in `test/weather-forecast-test.cpp`: the real captures (the 2026-10-28 morning capture has the remainder interval `11:00Z/PT3H`) and synthetic fixtures from `test/make-synthetic-fixtures.rb` for the cases listed in `weather-feed.md` section 7. `weather-cli` prints the lows. Extend the independent cross-check (`weather-feed.md` section 7) to the lows.
4. ✅ **T4. Display logic** (done: `barPixelsFor`, `notchBoundaryFor`, `weatherTopImage(haveHigh, highDelta, haveLow, lowDelta, out)`, and the pure rollover helpers `topDayOffset`/`topNightOffset` so the 06:00 and 17:00 switches are host-tested; 1998 checks) (`weather-display.h/.cpp`; encoding `Ambient-Display-Encoding.md` 3.1–3.2).
   - Remove `TOP_GROUP_PIXELS`, `BLEED_STEPS_PER_C`, `BLEED_PERCENT_PER_STEP`, `TopShape`, `topShapeFor()` and `blend()`.
   - `WEATHER_TYPICAL` becomes (35, 43, 38). `WEATHER_SCARLET` (173, 24, 0) and `WEATHER_SKY_BLUE` (38, 58, 66) are unchanged.
   - New: `TOP_PIXELS_PER_C = 2`, `NOTCH_PIXELS = 2`; a function for the signed bar length (one pixel per half degree, rounded down with the existing epsilon, capped at ±30; NaN and infinity handled as today) and one for the notch boundary $b$ (clamped to 1–59).
   - `weatherTopImage(haveHigh, highDeltaC, haveLow, lowDeltaC, out)`: background, then the bar, then the notch (black) over both.
   - Rewrite `test/weather-display-test.cpp`: no high and no low; ±0.49, ±0.5, an exact 7.0 (14 pixels), the 7.2 example (pixels 30–43), ±15 and beyond, NaN and infinity; the notch at the center, inside the bar, at the bar's tip, opposite the bar, and clamped at both ends; the −2.3 example (pixels 25 and 26).
5. ✅ **T5. Sketch** (done, not yet compiled for the ESP32 by Claude; in place of test-suite cases, the rollovers are covered by the host tests, because the suite's simulated clock does not move the weather, which uses `realNow()`) (`street-cleaning-revealed.ino`).
   - `LOW_AFTER_HOUR = 6` beside `TOMORROW_AFTER_HOUR = 17`: the night shown is the one beginning today from 06:00, the one that began yesterday before 06:00 (`Ambient-Display-Encoding.md` 3.4.2). Use `realNow()`, as the high does, so a simulated clock does not change it.
   - The typical low is for the date the night ends (the night's date + 1). Pass both deviations to `weatherTopImage()`.
   - Breathing: the gain applies to the background and the bar; the notch, being black, stays dark. The snow sparkle keeps `SPARKLE_LEVEL` and is not scaled by the background or the breath (the existing mix in the render already does this).
   - Serial: `low C|off` overrides tonight's low, like `high C|off`; `forecast` prints the lows held (yesterday's and today's nights at least); the verbose render report prints the bar length and the notch pixels in place of the `TopShape` line; update the command comments at the top of the sketch. Test-suite cases across the 06:00 and 17:00 rollovers.
   - Logging (`logging.md`): add the night's low to the forecast lines sent to Loki.
6. **T6. Hardware check.** Compile and upload, then on the installed strip:
   - Tune `WEATHER_TYPICAL` for the strip's white point: it should read as faintly green, not white or blue, at 75% of the bar colors' luminance.
   - Confirm a short bar (one or two pixels) and the notch are visible from 20 m in non-direct daylight (§2.2).
   - Confirm the snow sparkle stands out: its white (54, 54, 54) is only about a third brighter than the background and close to it in hue. If it is too faint, raise the top strip's flash level or dim the background further.
   - Photograph the strip in a few states and check them with a color-blindness simulator.
7. **T7. Documentation after wiring.** Record the measured heap in C4, and reconcile `weather-feed.md` 4.7 and `Ambient-Display-Encoding.md` section 3 with what was built.

*Exit criteria:* from the street, a viewer can tell a warm or cold forecast high from the side and length of the bar, and a warm or cold night from the side of the notch, without relying on color; the notch changes nights at 6 am and the bar days at 5 pm; a feed without `minTemperature` still drives everything except the notch; the host tests pass.

### Phase D — Deployment

1. ✅ **D1. Final hardware.** Recompile and deploy on a QuinLED Dig-Next-2.
2. ✅ **D2. Power architecture.** Mean Well GST90A12-P1M (12V 6.67 Amp 80W) sits indoors with the controller.
3. ✅ **D3. Connections and sealing.** Keep all connectors at weather-protected points; re-seal cut ends.
4. ✅ **D4. Strip mounting.** Mount to window frame with clips.

---

## 8. Command and procedure cheat-sheet

```sh
# Run from the project root (the folder containing src/)
FQBN=esp32:esp32:adafruit_feather_esp32_v2
PORT=/dev/cu.usbserial-5B1E0633431
SLOW=$FQBN:UploadSpeed=115200

# Compile and upload
arduino-cli compile --fqbn $SLOW src/street-cleaning-revealed
arduino-cli upload  --fqbn $SLOW -p $PORT src/street-cleaning-revealed

# Discover port after plugging in
ls /dev/cu.*

# If uploads fail with MD5 or flash-verify errors: erase first, then re-upload.
# (Locate the esptool bundled with the Espressif core.)
ESPTOOL=$(find ~/Library/Arduino15/packages/esp32/tools/esptool_py -type f -name 'esptool*' -perm +111 | head -1)
"$ESPTOOL" --port $PORT erase-flash

# One-time: libraries and secrets
arduino-cli lib install ArduinoJson          # 7.x; every .cpp in the sketch folder is compiled, so install it before building
# src/street-cleaning-revealed/secrets.h (git-ignored) must define WIFI_SSID, WIFI_PASS and NWS_USER_AGENT,
# and LOKI_URL (base URL), LOKI_USER and LOKI_TOKEN for Grafana Cloud logging (see docs/logging.md)
# (NWS asks for a contact email in the User-Agent; the snow emergency fetch reuses it)

# Host tests: forecast parser, top-strip display, snow risk, snow emergency feed (no hardware)
cd test && make test

# How the live city news feed is read (snow emergency notices, resulting state)
curl -s https://www.somervillema.gov/news.xml | ./snow-emergency-test -
```

**Power-up:** adapter first, then USB. **Power-down:** USB first, then adapter.

**Pre-power checks (unpowered, meter in continuity):** Feather GND ↔ chip pin 7 ↔ − rail ↔ adapter − terminal all beep; USB pin ↔ GND must **not** sustain a beep (a brief blip from the 0.1 µF capacitor charging is fine).

**If nothing lights:** (1) 4.5–5 V between chip pins 14 and 7 with USB on; (2) ~12.6 V across the adapter screws; (3) 470 Ω actually in the green path; (4) confirm the correct sketch is running; (5) swap green and blue.

---

## 9. Glossary

- **DI / DO:** Data in / data out on a pixel; data flows in the direction of the arrows printed on the strip.
- **BI:** Backup data input on WS2815. At the head of a strip, tie it to GND (per forum consensus and what worked here).
- **Level shifter:** Converts a 3.3 V logic signal to a higher-voltage logic signal; here a 74AHCT125 powered at 5 V.
- **Decoupling / bypass capacitor:** A small capacitor across a chip's supply pins that supplies brief current bursts and suppresses supply noise. A ceramic type has no polarity.
- **IP67 / IP68:** Ingress-protection ratings. IP67 = dust-tight and survives temporary immersion to 1 m for 30 min; IP68 = continuous immersion under specified conditions. Ratings do not cover UV, freeze–thaw cycling, or aging.
- **FQBN:** Fully-qualified board name used by `arduino-cli` (`vendor:arch:board`).
- **Active-low:** A pin that enables its function when held at ground (like the 74AHCT125's OE pins).

---

## 10. References consulted

- esptool GitHub issues: [#915](https://github.com/espressif/esptool/issues/915) and [#1019](https://github.com/espressif/esptool/issues/1019); [esptool troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/troubleshooting.html).
- Adafruit ESP32 Feather V2 product and pinout pages (board facts: chip, flash/PSRAM, pin labels).
- FastLED documentation and issue tracker (WS2815 support; `WS2812B` as portable choice).
- BTF-LIGHTING product listings and packaging text (wire colors, warnings, ratings).
- National Weather Service API (`api.weather.gov`, gridpoint `BOX/70,102`) and ArduinoJson 7.x; source selection and derived values are in `docs/weather-feed.md`.
- City of Somerville snow information (`somervillema.gov/snow`), City Alerts (`somervillema.gov/alerts`), the news RSS feed (`somervillema.gov/news.xml`), and the Everbridge developer and support documentation; source selection and the headline grammar are in `docs/snow-emergency-feed.md`.
