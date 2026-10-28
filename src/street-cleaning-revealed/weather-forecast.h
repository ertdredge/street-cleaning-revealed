// Somerville, MA weather outlook from the NWS gridpoint feed (api.weather.gov).
// Pure parsing and calculation: no Arduino.h, no WiFi, no FastLED, so it builds and is tested
// on a Mac (see "Testing" below). Fetching the live feed on the ESP32 is in weather-feed.h.
//
// What it provides, for TODAY and for TOMORROW (local calendar days, TZ must already be set):
//
//   tempPrecip(day, now) -> TempPrecip   forecast high (deg C) and precipitation probability (%, any kind)
//   snow(day, now)       -> SnowOutlook  snow amount, snow probability (%), snow start time
//
// The two groups are separate calls because they are normally used together: the high and the
// precipitation probability for one display, the three snow items for another.
//
// Dependencies: ArduinoJson 7.x (Arduino Library Manager: `arduino-cli lib install ArduinoJson`;
// for the host build see test/Makefile). NOTE: every .cpp in the sketch folder is compiled by the
// Arduino build whether or not it is #included, so install ArduinoJson before adding these files.
//
// ---- Where the numbers come from -----------------------------------------------------------
// One request to https://api.weather.gov/gridpoints/BOX/70,102 (about 290 KB; ArduinoJson's
// filter keeps only the few properties used, so the 290 KB is never held in memory; the filtered
// document measured about 30 KB of heap on a 64-bit host, so allow roughly that much free heap):
//   maxTemperature              daily high, deg C (deg F is converted if the feed ever says so)
//   probabilityOfPrecipitation  percent, in intervals of 1 to ~50 hours
//   snowfallAmount              mm, one interval per hour
// Per local day:
//   high           the maxTemperature interval that starts that day
//   precip         the largest probability among the intervals that overlap that day
//   snow amount    the sum of the hourly amounts inside that day (a storm crossing midnight is
//                  split between the two days; add today + tomorrow in the sketch for a storm total)
//   snow start     the first hour inside that day with measurable snow (an accumulation that began
//                  yesterday and continues reports midnight)
//   snow prob      NWS publishes no probability of snow accumulation, so this is a PROXY: the
//                  largest precipitation probability among the intervals that overlap the hours
//                  with measurable snow that day (0 when no snow is forecast). Read it as "chance
//                  of precipitation, given that the forecast calls for snow", not as a calibrated
//                  chance of reaching any particular depth.
// "Measurable" means at least ForecastConfig::minHourlySnowMm per hour (default 0.25 mm, about
// 0.01 inch); trace amounts below that are ignored. The city's emergency threshold of 4 inches is
// SNOW_EMERGENCY_MM; compare it yourself, this module makes no decision.
//
// Today's figures cover the feed's coverage window, which starts at the beginning of the current
// forecast cycle (about 7 a.m. in the morning cycle), not at midnight; hours already past drop out
// of the feed as the day goes on. The forecast high for today disappears from the feed once the
// afternoon is over, so update() carries the previous high forward for the same calendar day.
//
// ---- Failure policy --------------------------------------------------------------------------
// update() NEVER discards good data on failure: a failed fetch or parse leaves the previous
// snapshot in place and returns the reason. The snapshot stays usable for
// ForecastConfig::staleAfterSeconds (default 12 hours) after the last good update; after that the
// accessors return valid == false so the display can show "no data" instead of an old forecast.
// The snapshot remembers three local days, so "today" and "tomorrow" stay correct across midnight
// even if the next fetch has not succeeded yet.
//
// ---- Usage in the sketch (sketch-side code; this is an outline, not compiled here) -----------
//   #include "weather-forecast.h"
//   #include "weather-feed.h"                    // ESP32 only: HTTPS fetch
//   WeatherForecast forecast;                    // global
//   const char* const NWS_URL = "https://api.weather.gov/gridpoints/BOX/70,102";
//   const char* const NWS_UA  = "street-cleaning-revealed/1.0 (you@example.com)";  // NWS wants a contact
//
//   void pollWeather() {                         // call once an hour once WiFi is up and the clock is set
//     HttpsForecastSource feed;
//     feed.open(NWS_URL, NWS_UA);                // failure is fine: update() then reports FORECAST_IO_ERROR
//     ForecastStatus s = forecast.update(feed, time(nullptr));
//     feed.close();
//     if (s != FORECAST_OK) Serial.printf("weather: %s\n", forecastStatusName(s));
//   }
//
//   time_t now = time(nullptr);
//   TempPrecip tp = forecast.tempPrecip(FORECAST_TOMORROW, now);
//   if (tp.valid && !isnan(tp.highC)) { /* tp.highC, tp.precipPercent */ }
//   SnowOutlook sn = forecast.snow(FORECAST_TOMORROW, now);
//   if (sn.valid && sn.amountMm >= SNOW_EMERGENCY_MM) { /* sn.startTime, sn.probabilityPercent */ }
//
// ---- Testing (on a Mac, nothing to flash) ------------------------------------------------------
//   cd test
//   make deps     # once: downloads the ArduinoJson single header into test/vendor/
//   make test     # builds and runs weather-forecast-test against test/data fixtures
//   make cli      # builds weather-cli, a command-line front end:
//   ./weather-cli --file "data/BOX-70,102-10-28-2026-full.txt" --now "2026-10-28 15:14"
//   ./weather-cli --file data/synthetic/snow-tomorrow-6in.json --now "2026-10-28 15:14"
//   ./weather-cli --live                  # the real feed, via curl
//   ./weather-cli --url http://localhost:8080/gridpoint    # a mock server, via curl
// Fixtures are raw, unfiltered captures of the real response, plus synthetic variants made by
// test/make-synthetic-fixtures.rb (real structure, edited values). To point a program at a file
// instead of the network, give update() a FileForecastSource; on the device give it an
// HttpsForecastSource. update() cannot tell the difference.
#pragma once
#include <stddef.h>
#include <stdio.h>
#include <time.h>

// The city may declare a parking snow emergency when 4 or more inches are forecast.
const float SNOW_EMERGENCY_MM = 101.6f;

enum ForecastDay { FORECAST_TODAY = 0, FORECAST_TOMORROW = 1 };

enum ForecastStatus {
  FORECAST_OK,
  FORECAST_IO_ERROR,      // the source could not be opened, or delivered no bytes
  FORECAST_PARSE_ERROR,   // not valid JSON: truncated, an HTML error page, chunked-encoding debris
  FORECAST_SCHEMA_ERROR,  // valid JSON, but required series or units are missing or unexpected
  FORECAST_NO_MEMORY      // ArduinoJson could not allocate
};

// Short lowercase name for logging: "ok", "io error", "parse error", "schema error", "no memory".
const char* forecastStatusName(ForecastStatus s);

// ---- Byte sources ------------------------------------------------------------------------------
// The parser reads the feed from any ForecastSource. It satisfies ArduinoJson's reader contract
// (read() and readBytes()), so the JSON is streamed and never held in memory as text.
class ForecastSource {
 public:
  virtual ~ForecastSource() {}
  virtual int read() = 0;                              // next byte 0..255, or -1 at the end / on timeout
  virtual size_t readBytes(char* buf, size_t n);       // default: calls read() up to n times
};

// A file on disk (the test feed). open() returns false if the file cannot be opened.
class FileForecastSource : public ForecastSource {
 public:
  FileForecastSource() : f_(nullptr) {}
  explicit FileForecastSource(const char* path) : f_(nullptr) { open(path); }
  ~FileForecastSource() { close(); }
  bool open(const char* path);
  void close();
  bool isOpen() const { return f_ != nullptr; }
  int read() override;
  size_t readBytes(char* buf, size_t n) override;
 private:
  FileForecastSource(const FileForecastSource&);       // not copyable
  FileForecastSource& operator=(const FileForecastSource&);
  FILE* f_;
};

// A string already in memory. The data is not copied and must outlive the source.
class MemoryForecastSource : public ForecastSource {
 public:
  MemoryForecastSource(const char* data, size_t len) : p_(data), end_(data + len) {}
  int read() override { return p_ < end_ ? (unsigned char)*p_++ : -1; }
  size_t readBytes(char* buf, size_t n) override;
 private:
  const char* p_;
  const char* end_;
};

// ---- Results -----------------------------------------------------------------------------------
struct TempPrecip {
  bool  valid;           // false: no fresh data for that day (never fetched, stale, or day not in the snapshot)
  float highC;           // forecast high, deg C; NAN if the feed has no high for that day (e.g. today, late)
  int   precipPercent;   // probability of precipitation of any kind, 0..100; -1 if unknown
};

struct SnowOutlook {
  bool   valid;                // false: no fresh data for that day
  float  amountMm;             // forecast snow accumulation that day, mm (0 when none)
  int    probabilityPercent;   // PROXY, 0..100, see the header comment; 0 when no snow is forecast
  time_t startTime;            // first hour with measurable snow that day (epoch seconds); 0 when none
  float amountInches() const { return amountMm / 25.4f; }
  bool  snowForecast() const { return valid && startTime != 0; }
};

// ---- Configuration -----------------------------------------------------------------------------
struct ForecastConfig {
  float minHourlySnowMm;      // hourly snow below this is trace and ignored. Default 0.25 (about 0.01 in)
  long  staleAfterSeconds;    // accessors report valid == false this long after the last good update. Default 12 h
  ForecastConfig() : minHourlySnowMm(0.25f), staleAfterSeconds(12L * 3600L) {}
};

// Internal: one stored local day. Public only so the parser in the .cpp can fill it.
struct ForecastDayData {
  time_t dayStart;     // local midnight starting the day
  float  highC;        // NAN when unknown
  int    precip;       // -1 when unknown
  float  snowMm;
  int    snowProb;
  time_t snowStart;    // 0 when none
};
const int FORECAST_STORED_DAYS = 3;   // today, tomorrow, and the day after (rides over a midnight gap)

class WeatherForecast {
 public:
  explicit WeatherForecast(const ForecastConfig& cfg = ForecastConfig());

  // Reads one gridpoint response from `src` and, on success, replaces the snapshot. `now` is the
  // current time (epoch seconds, TZ set): it defines which local days are "today" and "tomorrow"
  // and starts the staleness clock. On any failure the previous snapshot is kept untouched and
  // the reason is returned (also available from lastStatus()). The time is dominated by receiving
  // the feed (about 290 KB over TLS); it uses roughly 30 KB of heap while parsing, freed on return.
  ForecastStatus update(ForecastSource& src, time_t now);

  // Forecast high and precipitation probability for `day`, as of `now`. Never blocks, never fetches.
  TempPrecip tempPrecip(ForecastDay day, time_t now) const;

  // Snow amount, probability proxy and start time for `day`, as of `now`. Never blocks, never fetches.
  SnowOutlook snow(ForecastDay day, time_t now) const;

  bool           hasData() const { return have_; }               // any good update yet (even if stale)
  time_t         lastGoodAt() const { return lastGood_; }        // `now` of the last good update, 0 if none
  time_t         feedUpdatedAt() const { return feedUpdated_; }  // NWS's own updateTime of that feed, 0 if unknown
  ForecastStatus lastStatus() const { return last_; }            // result of the most recent update()

 private:
  const ForecastDayData* find(ForecastDay day, time_t now) const;
  ForecastConfig cfg_;
  ForecastDayData days_[FORECAST_STORED_DAYS];
  bool have_;
  time_t lastGood_;
  time_t feedUpdated_;
  ForecastStatus last_;
};
