# Street Cleaning ReveaLED — Weather Feed

---

## 1. Purpose and scope

The display needs a short list of forecast values for Somerville, MA, for **today** and for **tomorrow**:

| Value | Used for |
|---|---|
| Snow accumulation amount | $inches$ in the snow removal risk (Ambient-Display-Encoding.md, 1.2) |
| Snow probability | $percent$ in the snow removal risk |
| Snow accumulation start time | $remaining$ in the snow removal risk (hours until the snow starts) |
| Forecast high temperature | Display; encoding not defined in this document |
| Probability of precipitation (any kind) | Display; encoding not defined in this document |

The city may declare a parking snow emergency when 4 or more inches of snow are forecast. City officials weigh pavement temperature, post-storm temperatures, snow already on the ground and other factors. None of that is duplicated here: only the forecast 4 inch criterion is tracked, as the constant $4 \text{ in} = 101.6 \text{ mm}$.

The weather module reports forecast values. It does not decide anything; turning the values into a risk is the job of the display logic.

A single location is monitored, so the grid cell is fixed at design time.

---

## 2. Source

### 2.1 Candidates

Considered: the US National Weather Service (api.weather.gov), Open-Meteo and OpenWeatherMap.

| | NWS | Open-Meteo | OpenWeatherMap |
|---|---|---|---|
| Forecast origin | Human-edited by the Boston/Norton forecast office | Numerical model output | Model output |
| Cost | Free | Free for non-commercial use | Free allowance, but the One Call API needs a payment card on file |
| API key | None | None | Required |
| Snow amount | Forecaster-edited hourly series | Derived mechanically from modeled liquid-equivalent precipitation | Model-derived |
| Same source as the city's officials | Very likely one of them | No | No |
| Developer friendliness | Plain JSON over HTTPS; large payload; identifying User-Agent requested | Very compact responses | Compact responses |

The cost and licensing entries reflect each provider's terms when the source was chosen.

Snow depth is the liquid equivalent multiplied by a snow-to-liquid ratio, and that ratio varies a great deal with the thermal structure of the atmosphere (Roebber et al. 2003). A fixed-ratio conversion from a model's liquid precipitation will misjudge borderline 3 to 5 inch events, which are the events that matter here. The forecaster-edited NWS series does not have that weakness to the same degree.

### 2.2 Decision

**NWS is the sole source.** It matches what the city is likely watching, costs nothing, needs no key and exposes the snow amount as a numeric hourly series. There is no secondary source: one robust failure policy (section 5) replaces redundancy, which keeps the logic single-path.

### 2.3 Request

One request per poll:

```
GET https://api.weather.gov/gridpoints/BOX/70,102
```

- The grid cell is resolved once from `/points/42.3876,-71.0995`, which returns office `BOX`, grid X 70, Y 102, and is then fixed in the design. The device never makes the lookup.
- `User-Agent` names the project and carries a contact address, as NWS requests, so that NWS can reach the owner if traffic ever misbehaves.
- `Accept: application/geo+json` (the default of `HttpsForecastSource::open()`'s `accept` argument; the snow emergency feed passes `application/rss+xml`, section 9). No `Accept-Encoding`, so the body arrives uncompressed and can be streamed straight into the parser.
- HTTP/1.0 is requested so the server does not use chunked transfer encoding; chunk-size lines in the middle of the JSON would be indistinguishable from a corrupt feed. If a server chunks anyway, `DechunkingSource` removes the framing (it tells the two apart by the first byte: JSON starts with `{`, a chunk size with a hex digit).
- Redirects are followed, since NWS can renumber grid cells.
- TLS verification is off by default (public, read-only data driving an art installation), with the option to pin a root certificate.

---

## 3. Feed structure

### 3.1 Properties used

The response carries about 60 forecast properties. Four of its fields are read:

| Field | Meaning | Unit in the feed |
|---|---|---|
| `updateTime` | When NWS last updated the grid | ISO 8601 timestamp |
| `maxTemperature` | Daily high | `wmoUnit:degC` |
| `probabilityOfPrecipitation` | Probability of precipitation of any type | `wmoUnit:percent` |
| `snowfallAmount` | Snow accumulation | `wmoUnit:mm` |

Each of the three series has the form `{ "uom": ..., "values": [ { "validTime": "<start>/<duration>", "value": <number> }, ... ] }`. The unit is read from `uom` and never assumed: degrees Fahrenheit are converted to Celsius, and centimeters and meters are converted to millimeters. Any other unit makes the feed unusable.

There is no snow-specific probability anywhere in the feed. `probabilityOfPrecipitation` is the chance of any precipitation, of any type.

### 3.2 Intervals

`validTime` is an ISO 8601 start timestamp, a slash, and an ISO 8601 duration. The intervals in a real capture (2026-10-28):

| Series | Intervals |
|---|---|
| `snowfallAmount` | All `PT1H`, contiguous, about 158 hours (6.6 days) |
| `probabilityOfPrecipitation` | 1 hour near the start, growing to 49 hours (`P2DT1H`) farther out |
| `maxTemperature` | One per day, 13 hours each (`PT13H`), starting 12:00Z (8 a.m. EDT, 7 a.m. EST) |

Durations therefore include a day component. The series start at the beginning of the current forecast cycle (11:00Z in the morning capture), not at the time of the request or at midnight, and hours that have passed drop out as the day goes on. Today's `maxTemperature` interval is no longer listed once its window has ended.

### 3.3 Size

A response is about 290 KB as served (indented), 130 KB without whitespace, and 13 KB gzipped. Most of it is properties the display does not use.

---

## 4. Derived values

All values are computed per **local calendar day** (US Eastern, `EST5EDT,M3.2.0,M11.1.0`, the same rule the sketch uses), because the feed's timestamps are in UTC and the viewer's "today" is not. A local day runs from one local midnight to the next, so on the daylight-saving changes it is 23 or 25 hours long. The sketch's clock must be set before any value is computed.

For a day $D$ = [$D_{start}$, $D_{end}$), an interval **overlaps** $D$ if it starts before $D_{end}$ and ends after $D_{start}$.

### 4.1 Forecast high

The `maxTemperature` interval that **starts** inside $D$. Its value is reported in °C, the unit the feed uses.

The feed drops today's high once its window has passed. The value last seen for the same calendar day is therefore kept, so today's high stays available for the rest of the day.

### 4.2 Probability of precipitation

The largest `probabilityOfPrecipitation` among the intervals that **overlap** $D$. Late in the forecast the intervals are many hours long, so a single interval can overlap two days and then contributes to both. If the feed omits a day's probability, the previously seen value for that day is kept.

### 4.3 Snow amount

Each hourly `snowfallAmount` value is split across the days it overlaps in proportion to the overlap, and summed per day:

$$
\text{amount}_D = \sum_{i}\; v_i \cdot \frac{|I_i \cap D|}{|I_i|}
$$

An hour of **trace** snow is ignored: an interval counts only if its rate is at least 0.25 mm per hour (about 0.01 inch). Reported in mm, with an inches accessor, 25.4 mm to the inch.

A storm that crosses midnight is split between its two days. The display logic (`snow-risk.cpp`) treats each day's snow as its own event, except that a day whose snow starts at local midnight continues the previous day's storm and is added to it; the per-day split keeps each value attributable to one calendar day.

### 4.4 Snow start time

The first measurable interval start inside $D$ (the later of the interval's start and $D_{start}$). A storm already in progress at midnight therefore reports midnight, not the hour it began. No snow in the day is reported as no start time.

### 4.5 Snow probability (proxy)

NWS publishes no probability of snow accumulation, so a proxy is used:

> the largest `probabilityOfPrecipitation` among the intervals that overlap the measurable-snow hours of $D$; 0 if the forecast has no measurable snow in $D$.

It reads as "the chance of precipitation, given that the forecast calls for snow". It is not a calibrated probability of reaching any depth. It maps onto the $percent$ input of the snow removal risk, with that caveat.

### 4.6 Coverage of today

Because the feed's coverage starts at the current forecast cycle (3.2), "today" figures describe the remainder of the day from that point, except for the carried-forward high and probability (4.1, 4.2). Tomorrow is fully covered.

---

## 5. Polling and failure handling

### 5.1 Cadence

**Hourly.** NWS updates the grid only a few times a day, so most polls return unchanged data, but the cost is 24 requests per day, which is negligible for the service, and an hourly cadence limits how long a missed update goes unnoticed. The poll runs only when WiFi is up and the clock is set.

### 5.2 Outcomes

Each fetch has exactly one outcome:

| Outcome | Cause |
|---|---|
| OK | A usable feed was parsed |
| I/O error | The connection or HTTP request failed, or no bytes arrived |
| Parse error | The body is not a JSON object: truncated, an HTML error page, corrupted transfer |
| Schema error | Valid JSON, but a required series is missing or empty, or a unit is not recognised (this includes NWS's error documents) |
| No memory | The parser could not allocate |

### 5.3 Keep the previous value

A failure of any kind **never discards good data**. The previous snapshot stays in place until a later fetch succeeds.

The snapshot **expires 12 hours after the last good update**; after that every accessor reports "no valid data", so the display shows a visible no-data state and never an old forecast presented as current.

The snapshot holds five local days (today through day 4). The sketch reads the first four (`FORECAST_DAYS_USED`, giving the snow risk at least 72 hours of look-ahead) and the fifth keeps all four correct after midnight even when the first fetch of the new day has not yet succeeded.

---

## 6. Architecture

The design separates the parts that need no hardware from the part that does, following the waste module (waste.h / waste.cpp).

| Part | Files | Depends on |
|---|---|---|
| Parser and calculator | `weather-forecast.h/.cpp` | ArduinoJson and the C library; no Arduino headers |
| ESP32 fetch layer | `weather-feed.h/.cpp` | Arduino HTTP and WiFi libraries |
| Display logic | The sketch | Both |

- **Input is a byte source.** The parser reads from an abstract source with two operations, read one byte and read a block. A file, a block of memory and the HTTPS connection implement it. The parser cannot tell them apart, which is what allows a saved file to stand in for the network.
- **Time is injected.** Every operation takes the current time as an argument. Nothing reads a clock internally, so any moment can be tested deterministically.
- **Streaming with a filter.** The response is never held as text. ArduinoJson's filter keeps only the fields in 3.1 while the bytes stream through, so memory depends on the number of intervals kept, not the size of the response: about 30 KB of heap measured on a 64-bit host.
- **Accessors return values, never fetch.** Fetching and reading are separate, so the display can read the latest snapshot at any time without blocking.
- **Values are grouped as they are used together.** The high and the precipitation probability are returned together; the snow amount, probability and start time are returned together.

---

## 7. Test strategy

- **The module is tested without hardware.** It is compiled on a Mac or Linux host with no Arduino headers, which is the reason for the split in section 6.
- **Fixtures are raw captures.** A fixture is the unmodified response, not a trimmed extract, so the tests exercise the same filter, key order, extra properties and interval formats the device sees.
- **Synthetic fixtures are real captures with values spliced in.** Only the values under test are edited (a storm, a unit, a missing high), so everything else in the file remains the real structure. Real captures of actual snow events complement them, because they can contain quirks that synthetic edits do not; the schema is identical across NWS forecast offices, so a capture from any US location is representative.
- **Cases covered:** the real feed; an amount well above the threshold; exactly 4 inches; a storm across midnight; trace snow; Fahrenheit; an unrecognised unit; today's high missing; the 25-hour fall-back day; truncated, HTML, error-document and empty responses; staleness; rollover past midnight; failure leaving the previous snapshot intact.
- **Independent cross-check.** The values computed from the real capture are compared with a second implementation in a different language, written separately.
- **A command-line front end** prints the values for a given file and time, and can read from a URL, which allows the same fixtures to be served by a mock HTTP server that returns errors, hangs or truncated bodies, to exercise the failure policy on the device. A mock server leaves TLS untested; TLS is exercised only against the real service.

---

## 8. Considered and not used

| Item | Reason |
|---|---|
| NWS `/forecast` endpoint | 13 KB and simple, but it has no numeric snow amount (only prose in `detailedForecast`), start times only to 12-hour periods, and today's daytime period disappears in the afternoon. It cannot supply the snow values, so the gridpoint feed is needed regardless. |
| Using `/forecast` as a cheap pre-check before the gridpoint fetch | Would reduce traffic outside winter but adds a second code path and a second set of failure modes. |
| NWS alerts (`/alerts/active?point=…`) and the gridpoint `hazards` property | A Winter Storm Watch or Warning would be a forecaster-set confidence signal. Not part of the design: the snow values do not depend on it. |
| The `weather` series (coverage words such as `slight_chance`, `chance`, `likely`) | Sparse and coarse (16-hour blocks, mostly beyond five days). Not used for the probability proxy. |
| Conditional requests (`ETag`) and gzip | Would cut the transfer; at 24 requests per day the full transfer is acceptable. |
| A second weather source | Section 2.2. |
| Calibrated snow probability (e.g. from the Weather Prediction Center's probabilistic snowfall products) | Published as images, not as a data feed. |

---

## 9. The snow emergency feed

City-declared parking snow emergencies come from a second source, the city's news RSS feed (`https://www.somervillema.gov/news.xml`). The full design is in `snow-emergency-feed.md`. This section covers what the sketch needs to wire it in. The wiring is done (Project-Plan.md, Phase S), pending the first ESP32 compile.

### 9.1 Relation to this module

| | Weather feed | Snow emergency feed |
|---|---|---|
| Source | NWS gridpoint JSON, about 290 KB | City news RSS, about 8 KB, 10 items |
| Pure module | `weather-forecast.h/.cpp` (needs ArduinoJson) | `snow-emergency.h/.cpp` (C library only) |
| Fetch | `HttpsForecastSource` | The same class, with `accept = "application/rss+xml"` |
| Result | A snapshot that expires after 12 hours | Two times (start and end of the parking ban), kept until the emergency ends |
| On a failed fetch | Keep the snapshot | Keep the state |
| Across a reboot | Lost (C6, optional) | Must be saved in NVS: the declaration leaves the feed within days |
| Display use | Snow events from the forecast (`snow-risk.h`) | A full-size snow event starting when the ban starts |

`HttpsForecastSource::open(url, userAgent, timeoutMs = 20000, rootCaPem = nullptr, accept = "application/geo+json")` is a plain HTTPS byte stream. `scanRss()` reads it to the end through `readBytes()`, exactly as the forecast parser does.

### 9.2 Interface used by the sketch

| Call | Purpose |
|---|---|
| `RssScanner rss;` `rss.reset();` | Holds the items of one fetch (about 7 KB; make it static, not a task-stack local) |
| `scanRss(feed, rss)` | Streams the body into the scanner; keeps each item's title, `pubDate` and link |
| `rss.sawFeed()` | False for an HTML error page or an empty body: keep the previous state |
| `SnowEmergency emergency;` | The tracker. `emergency.state` (`start`, `end`, `time_t`, 0 = unknown) is public for saving and restoring |
| `emergency.update(rss, now)` | Replays the feed oldest item first and applies every snow emergency notice; repeating it changes nothing |
| `emergency.phase(now)` | `SNOW_EMERGENCY_NONE`, `SNOW_EMERGENCY_DECLARED` (before the start) or `SNOW_EMERGENCY_IN_EFFECT` |
| `emergency.apply(parseSnowNotice(title, published), published, now)` | One headline by hand, for a serial test command |
| `snowEmergencyPhaseName()`, `snowNoticeKindName()` | Short names for serial output |

### 9.3 Wiring outline

1. **Fetch** hourly on core 0, after WiFi and the clock, one TLS session at a time (in the weather task after the forecast fetch, or a task of its own). Open with `accept = "application/rss+xml"`, `rss.reset()`, `scanRss`, `close`.
2. **Update** a copy of the tracker with `update(rss, realNow())` only if `sawFeed()`. Publish it under a mutex for the renderer on core 1, and save `state.start` and `state.end` to `Preferences` when they change. Restore them in `setup()`.
3. **Use** it in the snow removal risk. While `phase(realNow()) != SNOW_EMERGENCY_NONE`, add `SnowEvent{ true, SNOW_EMERGENCY_INCHES, 100, state.start }` to the events from `currentSnowEvents()`. `snowRiskFrom()` then gives the cleaning-risk curve up to the ban and 90 during it, on the bar that `snowRiskOnRightBar()` selects. Leave it out while the clock is simulated, as the real forecast is.
4. **Observe and test** with serial commands: `emergency` to print the state, phase and last fetch outcome, and an override in the style of `snow P IN H|off`.
