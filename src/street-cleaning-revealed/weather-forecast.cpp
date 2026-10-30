// See weather-forecast.h for what this computes, how to use it and how to test it.
#include "weather-forecast.h"
#include <ArduinoJson.h>
#include <cmath>
#include <stdint.h>
#include <string.h>

namespace {

// Days since 1970-01-01 for a Gregorian date; mon is 0..11 (Hinnant's days_from_civil).
long dayNumber(int y, int mon, int d) {
  int m = mon + 1;
  y -= (m <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  long yoe = y - era * 400;
  long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool readDigits(const char*& p, int n, int* out) {
  int v = 0;
  for (int i = 0; i < n; i++, p++) {
    if (*p < '0' || *p > '9') return false;
    v = v * 10 + (*p - '0');
  }
  *out = v;
  return true;
}

// Parses "2026-10-28T11:00:00+00:00" (or ...Z, or another offset). Returns the position just
// past it, or nullptr.
const char* parseTimestamp(const char* p, time_t* out) {
  int Y, M, D, h, m, s;
  if (!readDigits(p, 4, &Y) || *p++ != '-' || !readDigits(p, 2, &M) || *p++ != '-' || !readDigits(p, 2, &D) ||
      *p++ != 'T' || !readDigits(p, 2, &h) || *p++ != ':' || !readDigits(p, 2, &m) || *p++ != ':' ||
      !readDigits(p, 2, &s))
    return nullptr;
  if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || s > 60) return nullptr;
  long off = 0;
  if (*p == 'Z') {
    p++;
  } else if (*p == '+' || *p == '-') {
    int sign = (*p++ == '-') ? -1 : 1, oh, om;
    if (!readDigits(p, 2, &oh) || *p++ != ':' || !readDigits(p, 2, &om)) return nullptr;
    off = sign * (oh * 3600L + om * 60L);
  } else {
    return nullptr;
  }
  *out = (time_t)((int64_t)dayNumber(Y, M - 1, D) * 86400 + h * 3600L + m * 60L + s - off);
  return p;
}

// ISO 8601 duration as the feed writes them: P[nD][T[nH][nM][nS]] (also nW). Years and months
// are rejected: their length depends on the calendar and the feed doesn't use them.
bool parseDuration(const char* p, long* seconds) {
  if (*p++ != 'P') return false;
  bool inTime = false, any = false;
  long total = 0;
  while (*p) {
    if (*p == 'T') { inTime = true; p++; continue; }
    if (*p < '0' || *p > '9') return false;
    long n = 0;
    while (*p >= '0' && *p <= '9') n = n * 10 + (*p++ - '0');
    switch (*p++) {
      case 'W': total += n * 604800L; break;
      case 'D': total += n * 86400L; break;
      case 'H': if (!inTime) return false; total += n * 3600L; break;
      case 'M': if (!inTime) return false; total += n * 60L; break;
      case 'S': if (!inTime) return false; total += n; break;
      default: return false;
    }
    any = true;
  }
  *seconds = total;
  return any;
}

// "2026-10-28T11:00:00+00:00/PT1H" -> [start, end)
bool parseValidTime(const char* vt, time_t* start, time_t* end) {
  const char* p = parseTimestamp(vt, start);
  long d;
  if (!p || *p != '/' || !parseDuration(p + 1, &d) || d <= 0) return false;
  *end = *start + d;
  return true;
}

// Local midnight that starts the day `offsetDays` after the day containing `t`. The TZ must be
// set; DST days come out 23 or 25 hours long.
time_t localDayStart(time_t t, int offsetDays) {
  struct tm lt;
  localtime_r(&t, &lt);
  lt.tm_mday += offsetDays;
  lt.tm_hour = 0; lt.tm_min = 0; lt.tm_sec = 0;
  lt.tm_isdst = -1;
  return mktime(&lt);
}

bool getSeries(JsonVariantConst props, const char* name, JsonArrayConst* values, const char** uom) {
  JsonVariantConst s = props[name];
  *values = s["values"].as<JsonArrayConst>();
  *uom = s["uom"] | "";
  return !values->isNull() && values->size() > 0;
}

// A precipitation-probability interval, in seconds relative to the start of today.
struct PopInterval { int32_t s, e; int16_t pct; };
const int MAX_POP = 144;    // five days of hourly intervals is 120; anything beyond is ignored

// The feed is parsed into `out` (five local days from `now`). Nothing is written to the caller's
// snapshot here; update() decides whether to keep the result.
ForecastStatus parseFeed(ForecastSource& src, time_t now, const ForecastConfig& cfg,
                         ForecastDayData out[FORECAST_STORED_DAYS], time_t* feedUpdated) {
  static const char* const NEEDED[] = { "maxTemperature", "probabilityOfPrecipitation", "snowfallAmount" };

  // Keep only what we use: the feed has some 60 properties and most of its 290 KB is indentation.
  JsonDocument filter;
  filter["properties"]["updateTime"] = true;
  for (const char* name : NEEDED) {
    filter["properties"][name]["uom"] = true;
    filter["properties"][name]["values"][0]["validTime"] = true;
    filter["properties"][name]["values"][0]["value"] = true;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, src, DeserializationOption::Filter(filter));
  if (err == DeserializationError::NoMemory || doc.overflowed()) return FORECAST_NO_MEMORY;
  if (err == DeserializationError::EmptyInput) return FORECAST_IO_ERROR;
  if (err) return FORECAST_PARSE_ERROR;   // IncompleteInput, InvalidInput, TooDeep...
  if (!doc.is<JsonObject>()) return FORECAST_PARSE_ERROR;   // e.g. an HTML error page: the filtered parse skips it without an error

  JsonVariantConst props = doc["properties"];
  if (props.isNull()) return FORECAST_SCHEMA_ERROR;   // e.g. an NWS problem+json error body
  *feedUpdated = 0;
  parseTimestamp(props["updateTime"] | "", feedUpdated);

  // Local day boundaries, in seconds relative to the start of today.
  time_t ds[FORECAST_STORED_DAYS + 1];
  long rel[FORECAST_STORED_DAYS + 1];
  for (int i = 0; i <= FORECAST_STORED_DAYS; i++) {
    ds[i] = localDayStart(now, i);
    rel[i] = (long)(ds[i] - ds[0]);
  }
  for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
    out[i].dayStart = ds[i];
    out[i].highC = NAN;
    out[i].precip = -1;
    out[i].snowMm = 0.0f;
    out[i].snowProb = 0;
    out[i].snowStart = 0;
  }

  JsonArrayConst vals;
  const char* uom;

  // -- forecast highs: the interval that STARTS on a local day is that day's high
  if (!getSeries(props, "maxTemperature", &vals, &uom)) return FORECAST_SCHEMA_ERROR;
  bool fahrenheit;
  if (strcmp(uom, "wmoUnit:degC") == 0) fahrenheit = false;
  else if (strcmp(uom, "wmoUnit:degF") == 0) fahrenheit = true;
  else return FORECAST_SCHEMA_ERROR;
  for (JsonVariantConst e : vals) {
    time_t s, en;
    JsonVariantConst v = e["value"];
    if (v.isNull() || !parseValidTime(e["validTime"] | "", &s, &en)) continue;
    float c = v.as<float>();
    if (fahrenheit) c = (c - 32.0f) * 5.0f / 9.0f;
    for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
      if (s >= ds[i] && s < ds[i + 1]) {
        if (std::isnan(out[i].highC) || c > out[i].highC) out[i].highC = c;
        break;
      }
    }
  }

  // -- precipitation probability: largest value among the intervals overlapping each day
  if (!getSeries(props, "probabilityOfPrecipitation", &vals, &uom)) return FORECAST_SCHEMA_ERROR;
  if (strcmp(uom, "wmoUnit:percent") != 0) return FORECAST_SCHEMA_ERROR;
  PopInterval pop[MAX_POP];
  int npop = 0;
  for (JsonVariantConst e : vals) {
    time_t s, en;
    JsonVariantConst v = e["value"];
    if (v.isNull() || !parseValidTime(e["validTime"] | "", &s, &en)) continue;
    if (en <= ds[0] || s >= ds[FORECAST_STORED_DAYS] || npop >= MAX_POP) continue;
    pop[npop].s = (int32_t)(s - ds[0]);
    pop[npop].e = (int32_t)(en - ds[0]);
    pop[npop].pct = (int16_t)lroundf(v.as<float>());
    npop++;
  }
  for (int n = 0; n < npop; n++) {
    for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
      if (pop[n].s < rel[i + 1] && pop[n].e > rel[i] && pop[n].pct > out[i].precip) out[i].precip = pop[n].pct;
    }
  }

  // -- snow: split each interval across the days it covers; measurable hours set the start and
  //    the probability proxy
  if (!getSeries(props, "snowfallAmount", &vals, &uom)) return FORECAST_SCHEMA_ERROR;
  float toMm;
  if (strcmp(uom, "wmoUnit:mm") == 0) toMm = 1.0f;
  else if (strcmp(uom, "wmoUnit:cm") == 0) toMm = 10.0f;
  else if (strcmp(uom, "wmoUnit:m") == 0) toMm = 1000.0f;
  else return FORECAST_SCHEMA_ERROR;
  for (JsonVariantConst e : vals) {
    time_t s, en;
    JsonVariantConst v = e["value"];
    if (v.isNull() || !parseValidTime(e["validTime"] | "", &s, &en)) continue;
    if (en <= ds[0] || s >= ds[FORECAST_STORED_DAYS]) continue;
    float mm = v.as<float>() * toMm;
    long len = (long)(en - s);
    if (mm <= 0.0f || mm / (len / 3600.0f) < cfg.minHourlySnowMm) continue;   // none, or trace
    long rs = (long)(s - ds[0]), re = (long)(en - ds[0]);
    for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
      long a = rs > rel[i] ? rs : rel[i];
      long b = re < rel[i + 1] ? re : rel[i + 1];
      if (b <= a) continue;
      out[i].snowMm += mm * (float)(b - a) / (float)len;
      time_t start = ds[0] + a;
      if (out[i].snowStart == 0 || start < out[i].snowStart) out[i].snowStart = start;
      for (int n = 0; n < npop; n++) {
        if (pop[n].s < b && pop[n].e > a && pop[n].pct > out[i].snowProb) out[i].snowProb = pop[n].pct;
      }
    }
  }
  return FORECAST_OK;
}

}  // namespace

const char* forecastStatusName(ForecastStatus s) {
  switch (s) {
    case FORECAST_OK:           return "ok";
    case FORECAST_IO_ERROR:     return "io error";
    case FORECAST_PARSE_ERROR:  return "parse error";
    case FORECAST_SCHEMA_ERROR: return "schema error";
    case FORECAST_NO_MEMORY:    return "no memory";
  }
  return "?";
}

// ---- sources -------------------------------------------------------------------------------------
size_t ForecastSource::readBytes(char* buf, size_t n) {
  size_t i = 0;
  while (i < n) {
    int c = read();
    if (c < 0) break;
    buf[i++] = (char)c;
  }
  return i;
}

bool FileForecastSource::open(const char* path) {
  close();
  f_ = fopen(path, "rb");
  return f_ != nullptr;
}

void FileForecastSource::close() {
  if (f_) fclose(f_);
  f_ = nullptr;
}

int FileForecastSource::read() { return f_ ? fgetc(f_) : -1; }   // fgetc returns EOF (-1) at the end

size_t FileForecastSource::readBytes(char* buf, size_t n) { return f_ ? fread(buf, 1, n, f_) : 0; }

size_t MemoryForecastSource::readBytes(char* buf, size_t n) {
  size_t left = (size_t)(end_ - p_);
  if (n > left) n = left;
  memcpy(buf, p_, n);
  p_ += n;
  return n;
}

// ---- DechunkingSource ----------------------------------------------------------------------------
namespace {
int hexValue(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
}  // namespace

int DechunkingSource::rd() {
  if (peeked_ >= 0) { int c = peeked_; peeked_ = -1; return c; }
  return in_.read();
}

void DechunkingSource::sniff() {
  if (mode_ != UNKNOWN) return;
  int c = in_.read();
  if (c < 0) { mode_ = DONE; return; }
  peeked_ = c;
  mode_ = hexValue(c) >= 0 ? CHUNKED : PLAIN;
  wasChunked_ = (mode_ == CHUNKED);
}

bool DechunkingSource::nextChunk() {
  for (;;) {                                   // blank lines (the CRLF after chunk data) are skipped
    long size = 0;
    bool any = false;
    int c;
    while ((c = rd()) >= 0 && c != '\n') {
      if (c == '\r') continue;
      if (c == ';') { while ((c = rd()) >= 0 && c != '\n') {} break; }   // chunk extension: ignore
      int v = hexValue(c);
      if (v < 0) { mode_ = DONE; return false; }                         // not a chunk header: corrupt
      size = size * 16 + v;
      any = true;
    }
    if (c < 0) { mode_ = DONE; return false; }                           // ended inside a header
    if (!any) continue;
    if (size == 0) { mode_ = DONE; return false; }                       // the last chunk
    left_ = size;
    return true;
  }
}

int DechunkingSource::read() {
  char c;
  return readBytes(&c, 1) == 1 ? (unsigned char)c : -1;
}

size_t DechunkingSource::readBytes(char* buf, size_t n) {
  sniff();
  size_t got = 0;
  if (mode_ == PLAIN) {
    if (peeked_ >= 0 && n > 0) { buf[got++] = (char)peeked_; peeked_ = -1; }
    if (got < n) got += in_.readBytes(buf + got, n - got);
    out_ += got;
    return got;
  }
  while (mode_ == CHUNKED && got < n) {
    if (left_ < 0 && !nextChunk()) break;
    size_t want = (size_t)left_ < n - got ? (size_t)left_ : n - got;
    size_t r = in_.readBytes(buf + got, want);
    got += r;
    left_ -= (long)r;
    if (left_ == 0) left_ = -1;
    if (r < want) { mode_ = DONE; break; }    // the source ended or timed out inside a chunk
  }
  out_ += got;
  return got;
}

// ---- WeatherForecast -----------------------------------------------------------------------------
WeatherForecast::WeatherForecast(const ForecastConfig& cfg)
    : cfg_(cfg), have_(false), lastGood_(0), feedUpdated_(0), last_(FORECAST_IO_ERROR) {
  memset(days_, 0, sizeof days_);
}

ForecastStatus WeatherForecast::update(ForecastSource& src, time_t now) {
  ForecastDayData fresh[FORECAST_STORED_DAYS];
  time_t feedTime = 0;
  last_ = parseFeed(src, now, cfg_, fresh, &feedTime);
  if (last_ != FORECAST_OK) return last_;      // keep the previous snapshot

  // The feed drops a day's high, and the probabilities of its elapsed intervals, as the day
  // passes. Carry the earlier values forward for the same calendar day.
  if (have_) {
    for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
      for (int j = 0; j < FORECAST_STORED_DAYS; j++) {
        if (days_[j].dayStart != fresh[i].dayStart) continue;
        if (std::isnan(fresh[i].highC)) fresh[i].highC = days_[j].highC;
        if (fresh[i].precip < 0) fresh[i].precip = days_[j].precip;
      }
    }
  }
  memcpy(days_, fresh, sizeof days_);
  have_ = true;
  lastGood_ = now;
  feedUpdated_ = feedTime;
  return FORECAST_OK;
}

const ForecastDayData* WeatherForecast::find(ForecastDay day, time_t now) const {
  if (!have_ || now - lastGood_ > cfg_.staleAfterSeconds) return nullptr;
  time_t target = localDayStart(now, (int)day);
  for (int i = 0; i < FORECAST_STORED_DAYS; i++) {
    if (days_[i].dayStart == target) return &days_[i];
  }
  return nullptr;   // the snapshot is older than the days it holds
}

TempPrecip WeatherForecast::tempPrecip(ForecastDay day, time_t now) const {
  TempPrecip r = { false, NAN, -1 };
  const ForecastDayData* d = find(day, now);
  if (d && (!std::isnan(d->highC) || d->precip >= 0)) {
    r.valid = true;
    r.highC = d->highC;
    r.precipPercent = d->precip;
  }
  return r;
}

SnowOutlook WeatherForecast::snow(ForecastDay day, time_t now) const {
  SnowOutlook r = { false, 0.0f, 0, 0 };
  const ForecastDayData* d = find(day, now);
  if (d) {
    r.valid = true;
    r.amountMm = d->snowMm;
    r.probabilityPercent = d->snowProb;
    r.startTime = d->snowStart;
  }
  return r;
}
