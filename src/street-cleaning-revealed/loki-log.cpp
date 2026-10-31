#include "loki-log.h"
#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdarg.h>
#include "secrets.h"            // LOKI_URL, LOKI_USER, LOKI_TOKEN
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {

const int QUEUE_LINES = 24;              // ring size (about 6 KB)
const int LINE_CHARS = 256;              // longer lines are cut for Loki (Serial gets the whole line, to 383 chars)
const int BATCH_LINES = 10;              // lines per POST
const uint32_t MIN_SEND_GAP_MS = 10000;  // at most one POST per this long, unless a full batch is waiting
const uint32_t BACKOFF_MS[] = { 15000UL, 30000UL, 60000UL, 120000UL, 300000UL };
const char* const LEVEL_NAMES[] = { "debug", "info", "warn", "error" };

struct Entry {
  uint32_t ms;                  // millis() when logged
  uint8_t level;
  char text[LINE_CHARS];
};

Entry ring[QUEUE_LINES];
uint32_t headSeq = 0, tailSeq = 0;       // entries headSeq..tailSeq-1 are queued; entry s is ring[s % QUEUE_LINES]
uint32_t dropped = 0;
SemaphoreHandle_t mutex = nullptr;

uint32_t nextSendMs = 0, lastSendMs = 0;
int failures = 0;
bool everSent = false;

inline void lock() { if (mutex) xSemaphoreTake(mutex, portMAX_DELAY); }
inline void unlock() { if (mutex) xSemaphoreGive(mutex); }

// Copies `s` into `e.text` without its leading and trailing line breaks. False if nothing is left.
bool trimInto(Entry& e, const char* s) {
  while (*s == '\n' || *s == '\r') s++;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) n--;
  if (n == 0) return false;
  if (n > LINE_CHARS - 1) n = LINE_CHARS - 1;
  memcpy(e.text, s, n);
  e.text[n] = 0;
  return true;
}

void enqueue(LogLevel level, const char* text) {
  Entry e;
  e.ms = millis();
  e.level = (uint8_t)level;
  if (!trimInto(e, text)) return;
  lock();
  if (tailSeq - headSeq >= (uint32_t)QUEUE_LINES) { headSeq++; dropped++; }    // full: drop the oldest
  ring[tailSeq % QUEUE_LINES] = e;
  tailSeq++;
  unlock();
}

void vlog(LogLevel level, bool toLoki, const char* fmt, va_list ap) {
  char buf[384];
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  if (n < 0) return;
  if (n >= (int)sizeof buf) n = sizeof buf - 1;
  Serial.print(buf);
  if (n == 0 || buf[n - 1] != '\n') Serial.print("\n");
  if (toLoki) enqueue(level, buf);
}

// A bounded writer for building the JSON body.
struct Out {
  char* p; size_t cap, len; bool over;
  void put(char c) { if (len + 1 < cap) p[len++] = c; else over = true; }
  void puts(const char* s) { while (*s) put(*s++); }
  void str(const char* s) {                    // a JSON string body: escape quotes, backslashes and control characters
    for (; *s; s++) {
      unsigned char c = (unsigned char)*s;
      if (c == '"' || c == '\\') { put('\\'); put((char)c); }
      else if (c == '\n') puts("\\n");
      else if (c == '\r') puts("\\r");
      else if (c == '\t') puts("\\t");
      else if (c < 0x20) { char h[8]; snprintf(h, sizeof h, "\\u%04x", c); puts(h); }
      else put((char)c);
    }
  }
};

}  // namespace

void logBegin() {
  if (!mutex) mutex = xSemaphoreCreateMutex();
}

void logMsg(LogLevel level, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(level, true, fmt, ap);
  va_end(ap);
}

void logLocal(LogLevel level, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(level, false, fmt, ap);
  va_end(ap);
}

int lokiPending() {
  lock();
  int n = (int)(tailSeq - headSeq);
  unlock();
  return n;
}

int lokiFlush(uint64_t nowNs) {
  if (nowNs == 0) return 0;                                  // no valid clock yet: keep the lines
  uint32_t nowMs = millis();
  if ((int32_t)(nowMs - nextSendMs) < 0) return 0;           // backing off after a failure

  // Say so if lines were lost, once.
  lock();
  uint32_t lost = dropped;
  dropped = 0;
  unlock();
  if (lost) {
    char m[64];
    snprintf(m, sizeof m, "loki: %u log lines were dropped (queue full)", (unsigned)lost);
    enqueue(LOGLVL_WARN, m);
  }

  int pending = lokiPending();
  if (pending == 0) return 0;
  if (pending < BATCH_LINES && everSent && (uint32_t)(nowMs - lastSendMs) < MIN_SEND_GAP_MS) return 0;

  // Build the body under the lock (no network in here).
  const size_t CAP = 6400;
  char* body = (char*)malloc(CAP);
  if (!body) { logLocal(LOGLVL_WARN, "loki: out of memory building a batch\n"); return -1; }
  Out o = { body, CAP, 0, false };
  uint32_t sentTo;
  int count = 0;
  lock();
  uint32_t first = headSeq;
  count = (int)(tailSeq - headSeq);
  if (count > BATCH_LINES) count = BATCH_LINES;
  sentTo = first + (uint32_t)count;
  o.puts("{\"streams\":[");
  bool firstStream = true;
  for (int lvl = LOGLVL_DEBUG; lvl <= LOGLVL_ERROR; lvl++) {
    bool opened = false;
    for (int i = 0; i < count; i++) {
      const Entry& e = ring[(first + (uint32_t)i) % QUEUE_LINES];
      if (e.level != lvl) continue;
      if (!opened) {
        if (!firstStream) o.put(',');
        o.puts("{\"stream\":{\"app\":\"street-cleaning-reveaLED\",\"level\":\"");
        o.puts(LEVEL_NAMES[lvl]);
        o.puts("\"},\"values\":[");
        opened = true;
        firstStream = false;
      } else {
        o.put(',');
      }
      uint64_t ageNs = (uint64_t)(uint32_t)(nowMs - e.ms) * 1000000ULL;      // how long ago it was logged
      uint64_t ts = ageNs <= nowNs ? nowNs - ageNs : nowNs;
      char tsStr[24];
      snprintf(tsStr, sizeof tsStr, "%llu", (unsigned long long)ts);
      o.puts("[\""); o.puts(tsStr); o.puts("\",\""); o.str(e.text); o.puts("\"]");
    }
    if (opened) o.puts("]}");
  }
  o.puts("]}");
  unlock();
  if (o.over) {                                              // cannot happen with these sizes; do not send a cut-off body
    free(body);
    logLocal(LOGLVL_WARN, "loki: batch too large, dropping it\n");
    lock(); if (headSeq < sentTo) headSeq = sentTo; unlock();
    return -1;
  }
  body[o.len] = 0;

  // POST. Slow, so no lock is held.
  char url[320];
  size_t ul = strlen(LOKI_URL);
  while (ul > 0 && LOKI_URL[ul - 1] == '/') ul--;            // tolerate a trailing slash
  snprintf(url, sizeof url, "%.*s/loki/api/v1/push", (int)ul, LOKI_URL);
  WiFiClientSecure client;
  client.setInsecure();     // TODO: pin the Grafana Cloud root CA with client.setCACert(...), then drop this
  client.setTimeout(8);                                      // seconds
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  http.setReuse(false);
  int code = -1;
  if (http.begin(client, url)) {
    http.setAuthorization(LOKI_USER, LOKI_TOKEN);
    http.addHeader("Content-Type", "application/json");
    code = http.POST((uint8_t*)body, o.len);
    http.end();
  }
  free(body);
  lastSendMs = millis();

  if (code >= 200 && code < 300) {
    lock(); if (headSeq < sentTo) headSeq = sentTo; unlock();    // (the head may already be past it if the ring overflowed)
    failures = 0;
    everSent = true;
    return count;
  }
  // A 400 / 413 / 422 means this batch itself is refused (for example a timestamp too old), and retrying it
  // would only block everything behind it: drop it. Anything else (no connection, 401, 429, 5xx) may pass later.
  bool refused = code == 400 || code == 413 || code == 422;
  if (refused) { lock(); if (headSeq < sentTo) headSeq = sentTo; unlock(); }
  uint32_t wait = BACKOFF_MS[failures < 5 ? failures : 4];
  if (failures < 1000) failures++;
  nextSendMs = millis() + wait;
  logLocal(LOGLVL_WARN, "loki: POST failed (http %d)%s; trying again in %lu s\n", code,
           refused ? ", batch dropped" : "", (unsigned long)(wait / 1000UL));
  return -1;
}
#endif  // ARDUINO
