// Unit tests for weather-forecast.h/.cpp, run on a Mac or Linux host: `make test` in this directory.
// No framework: each CHECK prints its line number on failure; the exit status is nonzero if any failed.
// Fixtures are in data/: the real capture (historical/BOX-70,102-10-28-2026-full.json) and synthetic variants
// made from it by make-synthetic-fixtures.rb. Times are US Eastern, set below with a POSIX TZ rule.
#include "weather-forecast.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>

#ifndef DATA_DIR
#define DATA_DIR "data"
#endif

static int checks = 0, failures = 0;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
  printf("  FAIL line %d: %s\n", __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { checks++; double a_ = (a), b_ = (b); \
  if (!(fabs(a_ - b_) <= (tol))) { failures++; \
  printf("  FAIL line %d: %s = %g, expected %g (+-%g)\n", __LINE__, #a, a_, b_, (double)(tol)); } } while (0)
#define CHECK_STATUS(got, want) do { checks++; ForecastStatus g_ = (got); \
  if (g_ != (want)) { failures++; printf("  FAIL line %d: status %s, expected %s\n", __LINE__, \
  forecastStatusName(g_), forecastStatusName(want)); } } while (0)

static time_t local(int Y, int M, int D, int h, int m) {      // US Eastern wall clock
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m; t.tm_isdst = -1;
  return mktime(&t);
}
static time_t utc(int Y, int M, int D, int h, int m, int s = 0) {
  struct tm t = {};
  t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m; t.tm_sec = s;
  return timegm(&t);
}

static const char* RAW = "historical/BOX-70,102-10-28-2026-full.json";
static time_t NOW;   // 2026-10-28 15:14 local, when the real capture was taken; set in main() once TZ is set

// Loads data/<name> into `wf`.
static ForecastStatus load(WeatherForecast& wf, const char* name, time_t now) {
  std::string path = std::string(DATA_DIR) + "/" + name;
  FileForecastSource src;
  if (!src.open(path.c_str())) { printf("  cannot open %s\n", path.c_str()); return FORECAST_IO_ERROR; }
  return wf.update(src, now);
}
static ForecastStatus loadSynthetic(WeatherForecast& wf, const char* name, time_t now) {
  return load(wf, (std::string("synthetic/") + name).c_str(), now);
}

static void testRealCapture() {
  printf("real capture\n");
  WeatherForecast wf;
  CHECK(!wf.hasData());
  CHECK(!wf.tempPrecip(FORECAST_TODAY, NOW).valid);       // never updated
  CHECK(!wf.snow(FORECAST_TODAY, NOW).valid);

  CHECK_STATUS(load(wf, RAW, NOW), FORECAST_OK);
  CHECK(wf.hasData());
  CHECK(wf.lastGoodAt() == NOW);
  CHECK(wf.feedUpdatedAt() == utc(2026, 10, 28, 17, 56, 48));

  TempPrecip t = wf.tempPrecip(FORECAST_TODAY, NOW);
  CHECK(t.valid);
  CHECK_NEAR(t.highC, 13.3333, 0.001);          // 56 F, matching the /forecast capture
  CHECK(t.precipPercent == 2);
  t = wf.tempPrecip(FORECAST_TOMORROW, NOW);
  CHECK(t.valid);
  CHECK_NEAR(t.highC, 20.0, 0.001);             // 68 F
  CHECK(t.precipPercent == 2);

  for (int d = 2; d < FORECAST_DAYS_USED; d++) {   // the later days the snow risk reads are held too
    t = wf.tempPrecip((ForecastDay)d, NOW);
    CHECK(t.valid && !std::isnan(t.highC) && t.precipPercent >= 0);
  }
  for (int d = 0; d < FORECAST_DAYS_USED; d++) {   // no snow in the forecast
    SnowOutlook s = wf.snow((ForecastDay)d, NOW);
    CHECK(s.valid);
    CHECK_NEAR(s.amountMm, 0.0, 1e-6);
    CHECK(s.probabilityPercent == 0);
    CHECK(s.startTime == 0);
    CHECK(!s.snowForecast());
  }

  // staleness: good for 12 hours, then invalid
  CHECK(wf.tempPrecip(FORECAST_TODAY, NOW + 11 * 3600).valid);   // 02:14 next day: still fresh
  CHECK(!wf.tempPrecip(FORECAST_TODAY, NOW + 13 * 3600).valid);
  CHECK(!wf.snow(FORECAST_TOMORROW, NOW + 13 * 3600).valid);

  // rollover: just after midnight, with no new fetch, "today" is the snapshot's second day
  time_t late = local(2026, 10, 29, 0, 30);
  t = wf.tempPrecip(FORECAST_TODAY, late);
  CHECK(t.valid);
  CHECK_NEAR(t.highC, 20.0, 0.001);
  CHECK(t.precipPercent == 2);
  t = wf.tempPrecip(FORECAST_TOMORROW, late);
  CHECK(t.valid);
  CHECK_NEAR(t.highC, 20.5556, 0.001);
  CHECK(t.precipPercent == 1);
  // ...but not two days later than the snapshot holds
  CHECK(!wf.tempPrecip(FORECAST_TOMORROW, local(2026, 10, 30, 0, 30)).valid);

  // same data read from memory gives the same answer
  std::string path = std::string(DATA_DIR) + "/" + RAW;
  FILE* f = fopen(path.c_str(), "rb");
  CHECK(f != nullptr);
  if (f) {
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
    fclose(f);
    MemoryForecastSource mem(text.data(), text.size());
    WeatherForecast wm;
    CHECK_STATUS(wm.update(mem, NOW), FORECAST_OK);
    CHECK_NEAR(wm.tempPrecip(FORECAST_TOMORROW, NOW).highC, 20.0, 0.001);
  }
}

static void testSnowTomorrow() {
  printf("6 inches tomorrow\n");
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "snow-tomorrow-6in.json", NOW), FORECAST_OK);
  SnowOutlook s = wf.snow(FORECAST_TOMORROW, NOW);
  CHECK(s.valid && s.snowForecast());
  CHECK_NEAR(s.amountMm, 152.4, 0.01);
  CHECK_NEAR(s.amountInches(), 6.0, 0.001);
  CHECK(s.amountMm >= SNOW_EMERGENCY_MM);
  CHECK(s.startTime == utc(2026, 10, 29, 14, 0));          // 10:00 EDT
  CHECK(s.probabilityPercent == 90);
  SnowOutlook today = wf.snow(FORECAST_TODAY, NOW);
  CHECK(today.valid && !today.snowForecast());
  CHECK(today.probabilityPercent == 0);
  TempPrecip tp = wf.tempPrecip(FORECAST_TOMORROW, NOW);
  CHECK(tp.precipPercent == 90);
  CHECK_NEAR(tp.highC, 20.0, 0.001);
  CHECK(wf.tempPrecip(FORECAST_TODAY, NOW).precipPercent == 2);   // the 90% interval doesn't touch today
}

static void testBoundary() {
  printf("exactly 4 inches\n");
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "snow-boundary-4in.json", NOW), FORECAST_OK);
  SnowOutlook s = wf.snow(FORECAST_TOMORROW, NOW);
  CHECK_NEAR(s.amountMm, SNOW_EMERGENCY_MM, 0.01);
  CHECK_NEAR(s.amountInches(), 4.0, 0.001);
  CHECK(s.startTime == utc(2026, 10, 29, 18, 0));          // 14:00 EDT
  CHECK(s.probabilityPercent == 85);
}

static void testMidnightSplit() {
  printf("storm across midnight\n");
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "snow-midnight-split.json", NOW), FORECAST_OK);
  SnowOutlook a = wf.snow(FORECAST_TODAY, NOW), b = wf.snow(FORECAST_TOMORROW, NOW);
  CHECK_NEAR(a.amountMm, 20.0, 0.01);                       // 22:00 and 23:00
  CHECK(a.startTime == utc(2026, 10, 29, 2, 0));            // 22:00 EDT
  CHECK_NEAR(b.amountMm, 40.0, 0.01);                       // 00:00 to 03:59
  CHECK(b.startTime == utc(2026, 10, 29, 4, 0));            // midnight, not the storm's start
  CHECK_NEAR(a.amountMm + b.amountMm, 60.0, 0.01);          // the storm total is the sum
  CHECK(a.probabilityPercent == 70 && b.probabilityPercent == 70);
}

static void testTrace() {
  printf("trace snow is ignored\n");
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "snow-trace-only.json", NOW), FORECAST_OK);
  SnowOutlook s = wf.snow(FORECAST_TOMORROW, NOW);
  CHECK(s.valid && !s.snowForecast());
  CHECK_NEAR(s.amountMm, 0.0, 1e-6);
  ForecastConfig cfg;
  cfg.minHourlySnowMm = 0.05f;                              // a lower threshold counts the same hours
  WeatherForecast sensitive(cfg);
  CHECK_STATUS(loadSynthetic(sensitive, "snow-trace-only.json", NOW), FORECAST_OK);
  CHECK_NEAR(sensitive.snow(FORECAST_TOMORROW, NOW).amountMm, 0.8, 0.001);
}

static void testUnits() {
  printf("units\n");
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "highs-fahrenheit.json", NOW), FORECAST_OK);
  CHECK_NEAR(wf.tempPrecip(FORECAST_TODAY, NOW).highC, 13.3333, 0.001);
  CHECK_NEAR(wf.tempPrecip(FORECAST_TOMORROW, NOW).highC, 20.0, 0.001);

  WeatherForecast bad;
  CHECK_STATUS(loadSynthetic(bad, "snow-bad-unit.json", NOW), FORECAST_SCHEMA_ERROR);
  CHECK(!bad.hasData());
}

static void testFailuresKeepPreviousData() {
  printf("failures keep the previous snapshot\n");
  WeatherForecast wf;
  CHECK_STATUS(load(wf, RAW, NOW), FORECAST_OK);
  time_t later = NOW + 3600;
  CHECK_STATUS(loadSynthetic(wf, "truncated.json", later), FORECAST_PARSE_ERROR);
  CHECK_STATUS(loadSynthetic(wf, "error-page.html", later), FORECAST_PARSE_ERROR);
  CHECK_STATUS(loadSynthetic(wf, "problem.json", later), FORECAST_SCHEMA_ERROR);
  CHECK_STATUS(loadSynthetic(wf, "empty.json", later), FORECAST_IO_ERROR);
  CHECK_STATUS(loadSynthetic(wf, "snow-bad-unit.json", later), FORECAST_SCHEMA_ERROR);
  FileForecastSource missing;
  CHECK(!missing.open(DATA_DIR "/no-such-file.json"));
  CHECK_STATUS(wf.update(missing, later), FORECAST_IO_ERROR);   // an unopened source reads as empty
  CHECK(wf.lastStatus() == FORECAST_IO_ERROR);
  CHECK(wf.lastGoodAt() == NOW);                                // unchanged
  TempPrecip t = wf.tempPrecip(FORECAST_TOMORROW, later);
  CHECK(t.valid);
  CHECK_NEAR(t.highC, 20.0, 0.001);

  WeatherForecast fresh;                                        // with nothing to fall back on
  CHECK_STATUS(loadSynthetic(fresh, "truncated.json", NOW), FORECAST_PARSE_ERROR);
  CHECK(!fresh.hasData());
  CHECK(!fresh.tempPrecip(FORECAST_TODAY, NOW).valid);
}

static void testTodaysHighCarriedForward() {
  printf("today's high is carried forward once the feed drops it\n");
  time_t evening = local(2026, 10, 28, 20, 0);
  WeatherForecast alone;                                        // no earlier snapshot: the high is unknown
  CHECK_STATUS(loadSynthetic(alone, "no-high-today.json", evening), FORECAST_OK);
  TempPrecip t = alone.tempPrecip(FORECAST_TODAY, evening);
  CHECK(t.valid && isnan(t.highC) && t.precipPercent == 2);
  CHECK_NEAR(alone.tempPrecip(FORECAST_TOMORROW, evening).highC, 20.0, 0.001);

  WeatherForecast wf;                                           // afternoon fetch, then an evening fetch
  CHECK_STATUS(load(wf, RAW, NOW), FORECAST_OK);
  CHECK_STATUS(loadSynthetic(wf, "no-high-today.json", evening), FORECAST_OK);
  t = wf.tempPrecip(FORECAST_TODAY, evening);
  CHECK_NEAR(t.highC, 13.3333, 0.001);
  CHECK(wf.lastGoodAt() == evening);
}

static void testDstFallBack() {
  printf("fall-back day is 25 hours\n");
  time_t sat = local(2026, 10, 31, 20, 0);                      // Saturday evening, EDT
  WeatherForecast wf;
  CHECK_STATUS(loadSynthetic(wf, "snow-dst-fall-back.json", sat), FORECAST_OK);
  SnowOutlook today = wf.snow(FORECAST_TODAY, sat), tomorrow = wf.snow(FORECAST_TOMORROW, sat);
  CHECK_NEAR(today.amountMm, 5.0, 0.001);
  CHECK(today.startTime == utc(2026, 11, 1, 3, 0));             // Saturday 23:00 EDT
  CHECK_NEAR(tomorrow.amountMm, 5.0, 0.001);                    // Sunday's last hour counts...
  CHECK(tomorrow.startTime == utc(2026, 11, 2, 4, 0));          // ...23:00 EST
  // the 05:00Z hour (Monday 00:00 EST) belongs to neither
  CHECK_NEAR(wf.tempPrecip(FORECAST_TODAY, sat).highC, 13.8889, 0.001);      // Saturday
  CHECK_NEAR(wf.tempPrecip(FORECAST_TOMORROW, sat).highC, 16.6667, 0.001);   // Sunday, whose interval starts 07:00 EST
}

// An HTTP body sent with chunked transfer encoding (what a server may do despite an HTTP/1.0
// request) must give the same result as the plain body; a plain body must pass through untouched.
static std::string readAll(const char* name) {
  std::string path = std::string(DATA_DIR) + "/" + name, text;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return text;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
  fclose(f);
  return text;
}

static std::string chunkify(const std::string& body, const size_t* sizes, int nsizes, bool uppercase, bool extension) {
  std::string out;
  size_t pos = 0;
  int i = 0;
  while (pos < body.size()) {
    size_t n = sizes[i++ % nsizes];
    if (n > body.size() - pos) n = body.size() - pos;
    char hdr[32];
    snprintf(hdr, sizeof hdr, uppercase ? "%zX" : "%zx", n);
    out += hdr;
    if (extension) out += ";name=value";
    out += "\r\n";
    out.append(body, pos, n);
    out += "\r\n";
    pos += n;
  }
  out += "0\r\n\r\n";
  return out;
}

static void testChunkedTransfer() {
  printf("chunked transfer encoding\n");
  std::string body = readAll(RAW);
  CHECK(body.size() > 100000);
  static const size_t sizes[] = { 4096, 1, 7, 1000, 65536, 3 };

  for (int variant = 0; variant < 3; variant++) {
    std::string chunked = chunkify(body, sizes, 6, variant == 1, variant == 2);
    MemoryForecastSource mem(chunked.data(), chunked.size());
    DechunkingSource src(mem);
    WeatherForecast wf;
    CHECK_STATUS(wf.update(src, NOW), FORECAST_OK);
    CHECK(src.chunked());
    TempPrecip t = wf.tempPrecip(FORECAST_TODAY, NOW);
    CHECK(t.valid);
    CHECK_NEAR(t.highC, 13.3333, 0.001);
    CHECK(t.precipPercent == 2);
  }

  {                                                   // a plain body passes through unchanged
    MemoryForecastSource mem(body.data(), body.size());
    DechunkingSource src(mem);
    WeatherForecast wf;
    CHECK_STATUS(wf.update(src, NOW), FORECAST_OK);
    CHECK(!src.chunked());
    CHECK(src.bytesOut() > 0);
  }
  {                                                   // byte by byte gives the same bytes as in bulk
    std::string chunked = chunkify("{\"a\": [1, 2, 3]}", sizes, 6, false, false);
    MemoryForecastSource mem(chunked.data(), chunked.size());
    DechunkingSource src(mem);
    std::string got;
    int c;
    while ((c = src.read()) >= 0) got += (char)c;
    CHECK(got == "{\"a\": [1, 2, 3]}");
  }
  {                                                   // a chunked stream that ends early is a parse error, not a crash
    std::string chunked = chunkify(body, sizes, 6, false, false);
    chunked.resize(chunked.size() / 2);
    MemoryForecastSource mem(chunked.data(), chunked.size());
    DechunkingSource src(mem);
    WeatherForecast wf;
    CHECK_STATUS(wf.update(src, NOW), FORECAST_PARSE_ERROR);
    CHECK(!wf.hasData());
  }
  {                                                   // garbage that starts like a chunk size but is not
    const char junk[] = "deadbeef this is not a chunk header";
    MemoryForecastSource mem(junk, sizeof junk - 1);
    DechunkingSource src(mem);
    WeatherForecast wf;
    CHECK(wf.update(src, NOW) != FORECAST_OK);
  }
  {                                                   // an empty body
    MemoryForecastSource mem("", 0);
    DechunkingSource src(mem);
    WeatherForecast wf;
    CHECK_STATUS(wf.update(src, NOW), FORECAST_IO_ERROR);
  }
}

int main() {
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);                    // the same rule the sketch uses
  tzset();
  NOW = local(2026, 10, 28, 15, 14);
  testRealCapture();
  testSnowTomorrow();
  testBoundary();
  testMidnightSplit();
  testTrace();
  testUnits();
  testFailuresKeepPreviousData();
  testTodaysHighCarriedForward();
  testDstFallBack();
  testChunkedTransfer();
  printf("\n%d checks, %d failed\n", checks, failures);
  return failures ? 1 : 0;
}
