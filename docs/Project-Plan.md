# Street Cleaning ReveaLED — Project Plan

## 1. Purpose and one-paragraph summary

Year-round outdoor LED art installation that frames a window of roughly **105 cm × 160 cm**. The top and bottom strips are a fixed color then eventually an animated pattern. The **two side strips act as bar graphs** whose displayed value ranges over **0–21** and is **updated once a day**, with colors that can be changed and values calculated from calendar dates. Control is by an **ESP32 microcontroller running FastLED**. In a later phase the controller will use **WiFi and HTTPS** to monitor a web page for keywords and augment the 0–21 values with information gleaned from there.

---

## 2. Requirements and constraints

### 2.1 Functional

| Requirement | Detail |
|---|---|
| Geometry | Frame a window ~105 cm wide × ~160 cm tall. Perimeter ≈ 2 × (105 + 160) = **530 cm**. A single ~5 m reel is "slightly too short" for the full 530 cm; Opt to cut the one reel into four pieces totaling 300 pixels rather than buy another strip (see §3.1). |
| Top and bottom | Fixed color then eventually an animated pattern. |
| Sides | Bar graphs, values 0–21, updated once per day, colors changeable. |
| Firmware | FastLED on ESP32. |
| Later connectivity | WiFi + HTTPS to watch a web page for keywords and influence the bar values. |

### 2.2 Visibility

- **Nice-to-have:** visible from 150 m at dusk.
- **Wanted:** visible from 20 m in non-direct daylight.
- **Not required:** visibility in direct sun.

These were deliberately relaxed from an early "visible at 150 m in daylight" idea, which would have driven a much larger and costlier build.

### 2.3 Environment

- Outdoors, year-round (large temperature swings, rain, snow, freeze–thaw).
- The IP68 strip's own listing gives an operating range of **−20 °C to +40 °C**. Connection points still need protection from direct water.

### 2.4 Builder constraints and preferences

- Avoid soldering where practical (he owns a soldering iron, solder, multimeter, wire strippers, USB-C cables, electronics screwdrivers, and some ~16 AWG 2-conductor speaker wire; he is willing to solder where it genuinely saves trouble; self-described mediocre soldering skills).
- Avoid unneeded or overcomplicated parts.
- **Protan color vision deficiency**: red/dark green, blue/purple, and yellow/light green are often confused. Do not rely on distinguishing wire colors by eye alone; prefer electrical identification (continuity, diode-test, measurement), labels, and position. For the four pigtail wires at the strip's input (red, green, yellow, blue), he can tell them apart, and the meter results below corroborated the assumed roles.

---

## 3. Display overview

The project will illustrate the number of days remaining to a biweekly street cleaning event on the two sides of a street.  One side is second and fourth Fridays, the other side is first and third Thursdays.  Number of days until the next street sweeping event will be calculated and used as the basis for which LEDs are illuminated.

Trash and yard waste collection is every Wednesday on this street unless delayed to Thursday by a holliday.  Eventually this information will be incorporated into the display.

Snow emergencies are called by the city periodicallly and parking is prohibited on one side of the street or the other.  Evenutally this information will be retrieved from the internet and incorporated into the display.

### 3.1 Installation layout

- **Strip pieces, all cut from the one 300-pixel reel:** left side 90 pixels (about 1.5 m), right side 90 pixels (about 1.5 m), top 60 pixels (about 1.0 m), bottom 60 pixels (about 1.0 m). 90 + 90 + 60 + 60 = 300, so there is **no spare pixel**, and cuts must land exactly on the marked cut pads. The pieces are slightly shorter than the 160 cm / 105 cm window edges, replaces buying an extra strip.
- **Four separate strips**, joined at the corners by waterproof pigtail connectors.
- **Two independent 150-pixel runs, both starting at the lower-left corner:**

| Run | Data | Strips in order | Logical indexes |
|---|---|---|---|
| A | GPIO27 → 74AHCT125 gate 1 | left (bottom to top), then top (left to right) | 0–89 left, 90–149 top |
| B | second GPIO (TBD) → gate 2 | bottom (left to right), then right (bottom to top) | 150–209 bottom, 210–299 right |

  Both side bars fill upward as the index increases. The top-right corner has no joint: run A's last pixel (right end of the top strip) and run B's last pixel (top of the right strip) are both sealed dead ends there.
- **Electronics are stored indoors** (Feather, level shifter, 12 V supply). **Two cables** run from the electronics through one hole drilled in the window casing at the lower-left corner, one per run, to the head of run A (bottom of the left strip) and the head of run B (left end of the bottom strip). Each cable carries +12 V, GND, DATA, and BI (tied to GND at the head).
- **Why two runs rather than one 300-pixel daisy chain [estimates, not measured]:** voltage drop along a run scales roughly with the square of its length, so 150-pixel runs should show about a quarter of the drop of a single 300-pixel feed; each pigtail and joint carries at most half the current; a failed corner joint loses at most 60 or 90 pixels instead of 210; and it costs only a spare gate on the already-purchased 74AHCT125 plus one more GPIO. Rejected: a single pigtail feeding a 300-pixel chain around all four sides.
- **Firmware shape:** one 300-element array, two controllers, so the bar-graph logic can address a logical array:

```cpp
FastLED.addLeds<WS2812B, 27, GRB>(leds, 0, 150);       // run A: left + top
FastLED.addLeds<WS2812B, PIN_B, GRB>(leds, 150, 150);  // run B: bottom + right
```

- **Fusing:** fuse each 12 V feed separately (about 2–3 A) [recommended; not yet specified].

---

## 4. System architecture

### 4.1 Block diagram (bench configuration)

```
  Laptop USB-C ──► ESP32 Feather V2 (3.3 V logic)
                      │  USB pin (≈5 V)  ──────────────► 74AHCT125 pin 14 (VCC)
                      │  GND ──────────────────────────► breadboard − rail ──► 74AHCT125 pin 7
                      │  GPIO27 (silkscreen "27") ─────► 74AHCT125 pin 2 (1A)
                                                         74AHCT125 pin 3 (1Y) ─► 470 Ω ─► strip DI (green)

  12 V wall adapter (barrel → screw-terminal adapter)
      + screw ─► strip VCC (red) + 1000 µF capacitor (+ leg)
      − screw ─► strip GND (yellow) + capacitor stripe (− leg) ──► jumper to breadboard − rail
                                                         strip BI (blue) ─► breadboard − rail (GND)
```

Key principles:

1. **The strip and the Feather are separate power domains with a shared ground.** The 12 V supply powers only the strip. The Feather and the level-shifter chip are powered from USB (5 V).
2. **The 12 V never passes through the breadboard.** Breadboard contacts are commonly rated around 1 A. The 12 V rides on the screw terminals; the breadboard carries only logic, 5 V for the chip, and ground.
3. **Level shifting:** the ESP32 outputs 3.3 V logic, while WS2815 expects ~5 V-class logic referenced to a 12 V strip. A 74AHCT125 powered at 5 V shifts the signal up and buffers it.

### 4.2 Why these parts (rationale)

- **WS2815 (12 V, backup data line):** 12 V roughly halves the current for the same power compared with 5 V strips, which lowers voltage drop on long runs. The backup line (BI) lets the chain keep working if a single pixel dies. It speaks the same single-wire protocol as WS2812B.
- **ESP32 Feather V2:** WiFi built in, 8 MB flash, 2 MB PSRAM, USB-C, comfortable FastLED support, and the Feather form factor allows a terminal-block FeatherWing for a solder-free permanent install.
- **74AHCT125N level shifter (DIP-14):** through-hole, breadboard-friendly, inexpensive, and appropriate for a single data line. The "T" in AHCT means its inputs accept TTL-level (3.3 V) signals while running at 5 V.
- **470 Ω series resistor on the data line:** damps ringing and protects the first pixel.
- **1000 µF, 25 V electrolytic across the strip's power input:** absorbs inrush. It **must be rated well above 12 V**; an early candidate (4700 µF, 10 V) was rejected as under-rated.
- **0.1 µF ceramic bypass across chip pins 14 and 7:** local supply decoupling. Ceramic, so **no polarity**.

---

## 5. Bill of materials

### 5.1 Digi-Key

| Part | Role |
|---|---|
| Adafruit ESP32 Feather V2 (Digi-Key listing for Adafruit 5900) | Controller; the "with headers" variant keeps the build solder-free |
| SN74AHCT125N (2×) | Level shifter (one spare) |
| 1000 µF, 25 V radial electrolytic (2×) | Strip power-input capacitor (one spare) |
| C320C104K5R5TA, 0.1 µF ceramic radial (3×) | Chip bypass capacitors; replaced an earlier mistaken surface-mount (0603) pick |
| Mean Well LPV-150-12 | **Deployment** 12 V supply (IP67, bare flying leads on the AC side); not used on the bench |

### 5.2 Amazon

| Part | Role |
|---|---|
| BTF-LIGHTING WS2815 strip, **IP68**, with weatherproof pigtail connector | The LED strip (one reel for now) |
| BTF IP65 4-pin extension cables | Extension between connectors / to the controller area |
| WESIRI JST pigtails | Pigtail leads for connection |
| 12 V, 2 A wall adapter with barrel-to-screw-terminal adapter | **Bench** power (24 W) |
| Resistor assortment kit | Source of the 470 Ω resistor |
| HUAREW breadboard kit (breadboard, jumpers, power module) |

Items considered and dropped for the bench phase: a 22/3 wire (not needed on the bench), and a 12 V → 5 V buck converter module (a deployment item only; it would power the Feather and shifter from the 12 V supply once there is no USB).

### 5.3 Deferred / deployment parts (not yet bought)

- 12 V → 5 V buck converter (e.g., PlusRoc USB-C type) to feed the Feather/shifter from the 12 V supply.
- Adafruit Terminal Block Breakout FeatherWing (assembled), so pins are clamped by screws rather than friction fit.
- ~~Weatherproof enclosure(s)~~ Not needed now that the electronics are indoors. Still needed: sealant and strain relief where the two cables pass through the window casing, drip loops, and rated connectors or sealing for the cut strip ends.
- A second 470 Ω resistor (run B data line) and a second inline fuse (run B feed).
- Mains-side wiring solution for the LPV-150-12's bare AC leads (see §10.2).
- Mounting: **plastic** channel/clips or isolating tape (see §7.4).
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

**Planned for run B (not yet wired):** gate 2 carries run B. Pin 4 (2OE) stays on GND; pin 5 (2A) moves from GND to the second data GPIO (pin TBD; any free Feather GPIO); pin 6 (2Y) goes through a second 470 Ω resistor to run B's DI. Gates 3 and 4 stay tied low.

### 6.2 Strip wires (input end)

| Wire (observed) | Role | Connection |
|---|---|---|
| Yellow | GND | Adapter **−** screw (with capacitor stripe/− leg) and jumper to breadboard − rail |
| Blue | BACKUP (BI) | Breadboard − rail (GND) |
| Green | DATA (DI) | Via 470 Ω from chip pin 3 |
| Red | VCC, 12 V | Adapter **+** screw (with capacitor + leg) |

BTF's packaging lists VCC red, GND white, DATA green, BACKUP blue. The pigtail has **yellow where the packaging lists white**. Roles were checked electrically (see §7.2).

### 6.3 Feather ESP32 V2 facts

- Chip: ESP32-PICO-V3-02, 8 MB flash, 2 MB PSRAM, USB-C with CH9102F USB-serial.
- Data pin: **GPIO27**, silkscreen "27" (Adafruit's pinout page lists it as D27, with an analog alias). The sketch uses `#define DATA_PIN 27`.
- Onboard red LED: `LED_BUILTIN` (pin 13).
- The "USB" pin is the 5 V rail from the USB connector; the board's logic is 3.3 V.

---

## 7. What has been done and verified (bench log)

### 7.1 Toolchain (step 1.1) — **[verified]**

- macOS, Homebrew, `arduino-cli`, the Espressif **esp32 core** (esptool v5.3.1).
- Working directory: `~/dev/LED-parking/led`.
- Board FQBN: `esp32:esp32:adafruit_feather_esp32_v2`. Port: `/dev/cu.usbserial-5B1E0633431`. Both held in shell variables `FQBN` and `PORT`.
- Sketches are folders containing a same-named `.ino`.
- Blink sketch compiled (about 287 KB, 8% of flash) and now runs.

**Upload problems and how they resolved:**

1. First upload at 921600 baud failed: *"Unable to verify flash chip connection"* after the chip was detected.
2. Retry at 115200 (`$FQBN:UploadSpeed=115200`) wrote the bootloader then failed: *"MD5 of file does not match data in flash!"*
3. Research found the same symptom in esptool GitHub issues #915 and #1019, **both unresolved**; suspected write-protect bits or version regressions. Neither pointed to power, cable, or baud.
4. Different cable: no change. **`erase-flash` succeeded.** Separately, the Feather was removed from its shipping foam, where it had sat through all tests; an LED on each side turned off as pins left the foam.
5. After both changes, upload succeeded (still at 115200).

**Root cause is not established.** Two things changed at once: the erase, and the removal from the foam (which may be conductive and may have bridged pins). Treat this as an unresolved hypothesis. If it recurs: erase flash first, keep the board out of foam, keep the slow upload speed, and see esptool #915's `write_flash_status` workaround and #1019's `--no-compress` suggestion.

A later transient: the blink stopped after a while with USB still connected; replugging fixed it with no intervention. Cause unknown (possibly USB suspend/laptop sleep); no recurrence reported.

### 7.2 Electrical checks — **[verified]**

- **12 V adapter:** 12.64 V DC open-circuit, center-positive as labeled; the screw-terminal adapter's + and − screws matched.
- **Pigtail wires:** no shorts between any pair of the four wires.
- **Diode-mode readings (meter in diode test) with yellow as reference:**
  - Black on yellow, red on others: blue = OL (blocked), green = 1860, red = 198.
  - Red on yellow, black on others: blue = 640, green = 607, red = 197.
  - Interpretation: yellow is the GND-side anode to both signal pins (~0.6 V forward), red is symmetric/low relative to yellow (supply pair), blue/green are distinct signal lines. This is circumstantial but consistent with the packaging, and the strip later worked.
- **Breadboard continuity (unpowered):** Feather GND, chip pin 7, the − rail, and the adapter's − terminal all beep together; **no beep** between the Feather's USB pin and GND.

### 7.3 Things deliberately avoided

- The breadboard kit's **power module** (marked for 7–10 V in; the adapter is 12.64 V) is not used.

### 7.4 Strip warnings from the packaging (reference for deployment)

- Do **not** run the strip coiled; heat builds up.
- Do **not** let the strip's PCB backing touch **aluminum** channel (this contradicts an earlier suggestion of aluminum U-channel; use plastic channel or isolating tape).
- Rated −20 to +40 °C.
- The backup data (BI) wire at the head of a strip should go to the controller's Data or to GND; the forum consensus (and what worked here) is **GND**.

For first light, the reel was left in loose loops, one pixel at brightness 32, with a touch-test planned. The exception was explicitly judged a low-risk, short-duration choice, not manufacturer-sanctioned.

### 7.5 First-light firmware — **[verified]**

```cpp
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
```

Build/upload commands used (at slow speed):

```sh
cd ~/dev/LED-parking/led
arduino-cli lib install FastLED
SLOW=$FQBN:UploadSpeed=115200
arduino-cli compile --fqbn $SLOW firstlight
arduino-cli upload  --fqbn $SLOW -p $PORT firstlight
```

FastLED recent versions also offer a `WS2815` type; `WS2812B` is the version-independent choice and worked.

Power-up order used: **12 V adapter first, then USB.** Power-down: **USB first, then the adapter.** Result: pixel 0 (input end) dim white, all others dark. **Step 1 complete.**

---

## 8. Safety notes for the bench (keep these in force)

- Never connect or disconnect 12 V leads while the adapter is live unless needed; keep exposed screws/legs from touching.
- Ground connections first; disconnect last.
- A burning smell, any component getting warm, many pixels unexpectedly lit, or odd flashing: unplug the adapter first, then USB, and record what happened.
- Do not run many pixels coiled. Unroll before step 2 and beyond.
- The 12 V/2 A adapter limits sustained output: at full white, only about **1.3 m of strip** is supportable (the full reel is rated roughly 90 W, ~7.5 A, at full white). Keep brightness modest and avoid large areas of pure white during testing.
- Mains wiring is out of scope for casual work; see §10.2.

## 9. Remaining project plan

### Phase A — Bench step 2: color and programmatic control

Goals: confirm color order, control multiple pixels, and prototype the bar-graph logic, all on the uncut strip, unrolled.

1. **A1. Color-order check.** Light pixel 0 red, then green, then blue (separately). If red shows as green, correct the order from `GRB` to the right permutation.
2. **A2. Unroll and lay out.** Lay the strip out flat (or in wide, open serpentine runs with spacing). Keep brightness 32–64.
3. **A3. Multiple pixels.** Light a run of N pixels; verify addressing along the strip and that the far end is reachable. Measure the supply voltage at the strip input under a representative load.
4. **A4. Count and density.** Determine the actual LED count and density (LEDs per meter) from the order and by counting; replace the placeholder `NUM_LEDS 300`.
5. **A5. Bar-graph function.** Write `show_bar(value, color)` for 0–21, deciding how 21 levels map onto a 90-pixel side strip (about 4.3 pixels per step; pixels per step, partial steps, "off" color). Handle clamping and color changes. Test on a section of the uncut strip.
6. **A6. Input via serial.** Accept a value from the serial monitor (or a shell/Ruby script over serial) so the bar can be changed without reflashing.
7. **A7. Power check.** Estimate worst-case current for the chosen color scheme and confirm it fits the 2 A bench adapter.

*Exit criteria:* correct colors; a bar that fills 0–21 predictably; no heat or voltage problems at the chosen brightness.

### Phase B — Bench step 3: cut into four pieces; two independent 150-pixel runs

Lyout and rationale in §3.1.

1. **B1. Cut.** Cut the reel at the marked pads into 90 (left), 90 (right), 60 (top), 60 (bottom). Zero spare pixels, so mark and check each cut before making it.
2. **B2. Terminate and seal.** Six four-wire terminations are needed on cut ends: pigtail A to the left strip's DI end; the left strip's DO end; the top strip's DI end; pigtail B to the bottom strip's DI end; the bottom strip's DO end; the right strip's DI end. Two dead ends need sealing: the top strip's DO end and the right strip's DO end. The reel's two factory connectors can absorb up to two of these if they mate with the purchased BTF extension cables [unverified]. Solder versus snap-on connectors is still open (§10.2).
3. **B3. Wire run B's data path.** Gate 2 of the 74AHCT125 and a second 470 Ω resistor, per §6.1. Choose the second GPIO.
4. **B4. Update the sketch.** One 300-element array, two `addLeds` calls (see §3.1).
5. **B5. Test.** Each run individually, then together. Verify the index map (0–89 left bottom to top, 90–149 top left to right, 150–209 bottom left to right, 210–299 right bottom to top). Confirm each head's BI is on GND. Repeat the full-length voltage measurement per run at the real feed.

Considerations:

- **Cut only at marked cut points** (copper pads); corners are joins between separate strips rather than 90° bends in a strip.
- The IP68 strip is silicone-sealed; **cut ends must be re-sealed** and need connectors (or soldering). The "no solder" goal is unlikely to hold fully here.
- **Each run head needs** BI to GND and DI fed with data from its own gate.
- **Power:** each run gets its own parallel feed from the 12 V supply, not series.
- Retest each piece individually before testing together.

### Phase C — Network and daily update logic

1. **C1. WiFi bring-up.** Connect the ESP32 to WiFi; keep credentials out of version control (e.g., a git-ignored `secrets.h`).
2. **C2. Time.** Use NTP so the "once per day" schedule is meaningful; handle timezone (America/New_York) and DST.
3. **C3. HTTPS fetch.** Use `WiFiClientSecure` with proper certificate validation (a root CA bundle or pinned certificate), not `setInsecure()`. Note that TLS roots expire; plan for updating them.
4. **C4. Keyword parse.** Define keyword/page signal.
5. **C5. Daily scheduling.** Decide between staying awake and using deep sleep for a once-daily wake. If the device is on the 12 V-derived 5 V supply continuously, staying awake is simpler and not a significant power cost.
6. **C6. Persistence.** Store the last good value in NVS flash so a power loss does not lose state.
7. **C7. Server-side alternative.** Consider moving the scraping to a small script or Apps Script endpoint that exposes a tiny JSON value for the ESP32 to read.
8. **C8. Observability.** An easy way to see last-update time and last value (serial log, a status LED pattern, or a trivial web endpoint).

### Phase D — Deployment design

1. **D1. Power architecture.** LPV-150-12 (IP67, 12 V, 150 W class) sits indoors with the controller; add a fuse per run; run two fused 12 V feeds out through the two cables; add the 12 V → 5 V buck converter for the Feather (no USB in deployment).
2. **D2. Controller placement (indoors).** No outdoor enclosure needed. Mount the Feather on the terminal-block FeatherWing and keep the 74AHCT125 and passives on a small board in an ordinary indoor enclosure or mount; keep data wires short and twist each data wire with a ground wire if the run to the casing exceeds about 2 m. Outdoor-facing items are the two cables, the casing penetration (seal it, add a drip loop), and the strip joints.
3. **D3. Strip mounting.** Plastic channel or isolating tape (not bare aluminum against the PCB). Plan expansion/contraction and adhesive performance across winter.
4. **D4. Connections and sealing.** Keep all connectors at weather-protected points; re-seal cut ends; consider drip loops at every cable run.
5. **D5. Thermal envelope.** The strip is rated −20 to +40 °C. Decide whether to accept the risk in winter extremes or add mitigation; document the decision.
6. **D6. Mains wiring.** See §10.2.
7. **D7. Burn-in.** A multi-day run at deployment brightness on the bench or in place before the final mounting.

---

## 10. Open issues, risks, and decisions to make

### 10.1 Mains power (important)

The LPV-150-12 has bare flying leads on its AC side. These must be connected to mains wiring using appropriately rated, enclosed, strain-relieved connections, on a GFCI-protected circuit suitable for exterior use.

---

## 11. Command and procedure cheat-sheet

```sh
# Shell variables used in the working directory ~/dev/LED-parking/led
FQBN=esp32:esp32:adafruit_feather_esp32_v2
PORT=/dev/cu.usbserial-5B1E0633431
SLOW=$FQBN:UploadSpeed=115200

# Compile and upload
arduino-cli compile --fqbn $SLOW <sketch_folder>
arduino-cli upload  --fqbn $SLOW -p $PORT <sketch_folder>

# Discover port after plugging in
ls /dev/cu.*

# If uploads fail with MD5 or flash-verify errors: erase first, then re-upload.
# (Locate the esptool bundled with the Espressif core.)
ESPTOOL=$(find ~/Library/Arduino15/packages/esp32/tools/esptool_py -type f -name 'esptool*' -perm +111 | head -1)
"$ESPTOOL" --port $PORT erase-flash
```

**Power-up:** adapter first, then USB. **Power-down:** USB first, then adapter.

**Pre-power checks (unpowered, meter in continuity):** Feather GND ↔ chip pin 7 ↔ − rail ↔ adapter − terminal all beep; USB pin ↔ GND must **not** sustain a beep (a brief blip from the 0.1 µF capacitor charging is fine).

**If nothing lights:** (1) 4.5–5 V between chip pins 14 and 7 with USB on; (2) ~12.6 V across the adapter screws; (3) 470 Ω actually in the green path; (4) confirm the correct sketch is running; (5) swap green and blue.

---

## 12. Glossary

- **DI / DO:** Data in / data out on a pixel; data flows in the direction of the arrows printed on the strip.
- **BI:** Backup data input on WS2815. At the head of a strip, tie it to GND (per forum consensus and what worked here).
- **Level shifter:** Converts a 3.3 V logic signal to a higher-voltage logic signal; here a 74AHCT125 powered at 5 V.
- **Decoupling / bypass capacitor:** A small capacitor across a chip's supply pins that supplies brief current bursts and suppresses supply noise. A ceramic type has no polarity.
- **IP67 / IP68:** Ingress-protection ratings. IP67 = dust-tight and survives temporary immersion to 1 m for 30 min; IP68 = continuous immersion under specified conditions. Ratings do not cover UV, freeze–thaw cycling, or aging.
- **FQBN:** Fully-qualified board name used by `arduino-cli` (`vendor:arch:board`).
- **Active-low:** A pin that enables its function when held at ground (like the 74AHCT125's OE pins).

---

## 13. References consulted in the conversation

- esptool GitHub issues: [#915](https://github.com/espressif/esptool/issues/915) and [#1019](https://github.com/espressif/esptool/issues/1019); [esptool troubleshooting](https://docs.espressif.com/projects/esptool/en/latest/troubleshooting.html).
- Adafruit ESP32 Feather V2 product and pinout pages (board facts: chip, flash/PSRAM, pin labels).
- FastLED documentation and issue tracker (WS2815 support; `WS2812B` as portable choice).
- BTF-LIGHTING product listings and packaging text (wire colors, warnings, ratings).

