// Command-line front end for weather-forecast.h/.cpp, for trying the module on a Mac with a saved
// capture, a mock server, or the real feed. Build: `make cli` in this directory.
//
//   weather-cli --file PATH            read a saved response (a capture from curl, or a fixture)
//   weather-cli --url URL              fetch URL with curl (so it can be http://localhost:8080/... for a mock server)
//   weather-cli --live                 fetch the real Somerville feed with curl
//   options: --now "YYYY-MM-DD HH:MM[:SS]"   the time to evaluate at, US Eastern (default: the real clock);
//                                            use it with --file, since a capture ages
//            --user-agent STRING             sent with --url/--live (NWS wants an identifying contact)
//
// Exit status: 0 on success, 1 if the feed could not be read or parsed, 2 on bad usage.
#include "weather-forecast.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char* LIVE_URL = "https://api.weather.gov/gridpoints/BOX/70,102";

// A response fetched by running curl. The URL and user agent are placed in single quotes, so they
// must not contain one.
class CurlForecastSource : public ForecastSource {
 public:
  CurlForecastSource() : p_(nullptr) {}
  ~CurlForecastSource() { if (p_) pclose(p_); }
  bool open(const char* url, const char* userAgent) {
    if (strchr(url, '\'') || strchr(userAgent, '\'')) return false;
    char cmd[1024];
    snprintf(cmd, sizeof cmd, "curl -fsS -m 30 -A '%s' -H 'Accept: application/geo+json' '%s'", userAgent, url);
    p_ = popen(cmd, "r");
    return p_ != nullptr;
  }
  int read() override { return p_ ? fgetc(p_) : -1; }
  size_t readBytes(char* buf, size_t n) override { return p_ ? fread(buf, 1, n, p_) : 0; }
 private:
  FILE* p_;
};

static void printTime(const char* label, time_t t) {
  char buf[48] = "-";
  if (t) {
    struct tm lt;
    localtime_r(&t, &lt);
    strftime(buf, sizeof buf, "%a %Y-%m-%d %H:%M %Z", &lt);
  }
  printf("%s%s", label, buf);
}

static void printDay(const char* name, const WeatherForecast& wf, ForecastDay day, time_t now) {
  TempPrecip tp = wf.tempPrecip(day, now);
  SnowOutlook sn = wf.snow(day, now);
  printf("%-9s ", name);
  if (!tp.valid) printf("high ?   precip ?    ");
  else {
    if (isnan(tp.highC)) printf("high ?   "); else printf("high %.1f C  ", tp.highC);
    if (tp.precipPercent < 0) printf("precip ?    "); else printf("precip %d%%  ", tp.precipPercent);
  }
  if (!sn.valid) printf("| snow ?\n");
  else if (!sn.snowForecast()) printf("| snow none\n");
  else {
    printf("| snow %.1f mm (%.1f in)  probability %d%%  starts ", sn.amountMm, sn.amountInches(), sn.probabilityPercent);
    printTime("", sn.startTime);
    printf("%s\n", sn.amountMm >= SNOW_EMERGENCY_MM ? "  [>= 4 in]" : "");
  }
}

int main(int argc, char** argv) {
  const char* file = nullptr;
  const char* url = nullptr;
  const char* nowArg = nullptr;
  const char* ua = "street-cleaning-revealed-cli/1.0 (test)";
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--file") && i + 1 < argc) file = argv[++i];
    else if (!strcmp(argv[i], "--url") && i + 1 < argc) url = argv[++i];
    else if (!strcmp(argv[i], "--live")) url = LIVE_URL;
    else if (!strcmp(argv[i], "--now") && i + 1 < argc) nowArg = argv[++i];
    else if (!strcmp(argv[i], "--user-agent") && i + 1 < argc) ua = argv[++i];
    else { file = nullptr; url = nullptr; break; }
  }
  if ((!file) == (!url)) {
    fprintf(stderr, "usage: %s (--file PATH | --url URL | --live) [--now \"YYYY-MM-DD HH:MM[:SS]\"] [--user-agent STRING]\n", argv[0]);
    return 2;
  }

  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);       // the same rule the sketch uses
  tzset();
  time_t now = time(nullptr);
  if (nowArg) {
    int Y, M, D, h, m, s = 0;
    if (sscanf(nowArg, "%d-%d-%d %d:%d:%d", &Y, &M, &D, &h, &m, &s) < 5) { fprintf(stderr, "bad --now\n"); return 2; }
    struct tm t = {};
    t.tm_year = Y - 1900; t.tm_mon = M - 1; t.tm_mday = D; t.tm_hour = h; t.tm_min = m; t.tm_sec = s; t.tm_isdst = -1;
    now = mktime(&t);
  }

  WeatherForecast wf;
  ForecastStatus st;
  if (file) {
    FileForecastSource src;
    if (!src.open(file)) { fprintf(stderr, "cannot open %s\n", file); return 1; }
    st = wf.update(src, now);
  } else {
    CurlForecastSource src;
    if (!src.open(url, ua)) { fprintf(stderr, "cannot start curl\n"); return 1; }
    st = wf.update(src, now);
  }
  if (st != FORECAST_OK) { fprintf(stderr, "forecast: %s\n", forecastStatusName(st)); return 1; }

  printTime("now:  ", now);
  printTime("    feed updated: ", wf.feedUpdatedAt());
  printf("\n");
  printDay("today", wf, FORECAST_TODAY, now);
  printDay("tomorrow", wf, FORECAST_TOMORROW, now);
  printDay("day 2", wf, FORECAST_DAY_2, now);
  printDay("day 3", wf, FORECAST_DAY_3, now);
  return 0;
}
