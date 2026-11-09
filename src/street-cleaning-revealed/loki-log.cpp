#include "loki-log.h"
#ifdef ARDUINO
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <stdarg.h>
#include <string.h>
#include "secrets.h"            // LOKI_URL, LOKI_USER, LOKI_TOKEN
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#ifndef LOKI_FUNC_MODE
#define LOKI_FUNC_MODE 0         // 0: structured metadata {"func": ...}; 1: "func=NAME " prefix on the line
#endif

namespace {

const uint32_t RING_BYTES = 12288;       // queue storage (static); a record is a 12-byte header plus the text
const int LINE_CHARS = 320;              // longer lines are cut for Loki (Serial gets the whole line, to 383 chars)
const int BATCH_MAX_LINES = 40;          // lines per POST, at most (the body size limit usually comes first)
const int SEND_NOW_LINES = 20;           // this many waiting: send without waiting for the minimum gap
const uint32_t MIN_SEND_GAP_MS = 10000;  // otherwise at most one POST per this long
const uint32_t BACKOFF_MS[] = { 15000UL, 30000UL, 60000UL, 120000UL, 300000UL };
const char* const LEVEL_NAMES[] = { "debug", "info", "warn", "error" };
const char* const KIND_NAMES[] = { "log", "heartbeat" };
const int NUM_KINDS = 2;
const uint16_t SKIP = 0xFFFF;            // header length marking "the rest of the ring is padding"

struct Rec {
  uint32_t ms;                  // millis() when logged
  const char* func;             // static string (__func__)
  uint16_t len;                 // text length including the terminating NUL
  uint8_t level;
  uint8_t kind;
};

// Variable-length records in a ring: head..tail, `used` bytes (including end-of-ring padding). A record never
// wraps: if it will not fit before the end, the rest of the ring is padding and the record starts at 0.
alignas(8) uint8_t ring[RING_BYTES];
uint32_t headPos = 0, tailPos = 0, used = 0;
int queued = 0;                          // records in the ring
uint32_t poppedTotal = 0;                // records ever removed (sent or dropped); identifies a batch even if drops happen meanwhile
uint32_t dropped = 0;                    // dropped since the last warning
uint32_t droppedTotal = 0, sentTotal = 0, postFailuresTotal = 0;
SemaphoreHandle_t mutex = nullptr;

uint32_t nextSendMs = 0, lastSendMs = 0;
int failures = 0;
bool everSent = false;

inline void lock() { if (mutex) xSemaphoreTake(mutex, portMAX_DELAY); }
inline void unlock() { if (mutex) xSemaphoreGive(mutex); }

inline uint32_t recSize(uint32_t textLen) { return ((uint32_t)sizeof(Rec) + textLen + 3u) & ~3u; }

struct Cursor { uint32_t pos, left; };

// Reads the record at the cursor and moves past it (skipping padding). False when no records are left.
bool readNext(Cursor& c, Rec& r, const char** text) {
  while (c.left > 0) {
    uint32_t toEnd = RING_BYTES - c.pos;
    if (toEnd < sizeof(Rec)) { c.left -= toEnd < c.left ? toEnd : c.left; c.pos = 0; continue; }
    memcpy(&r, ring + c.pos, sizeof r);
    if (r.len == SKIP) { c.left -= toEnd < c.left ? toEnd : c.left; c.pos = 0; continue; }
    uint32_t sz = recSize(r.len);
    if (text) *text = (const char*)(ring + c.pos + sizeof(Rec));
    c.pos += sz;
    if (c.pos >= RING_BYTES) c.pos = 0;
    c.left -= sz < c.left ? sz : c.left;
    return true;
  }
  return false;
}

// Removes the oldest record. Call with the lock held.
bool popOldest() {
  Cursor c = { headPos, used };
  Rec r;
  if (!readNext(c, r, nullptr)) { headPos = tailPos = used = 0; queued = 0; return false; }
  headPos = c.pos;
  used = c.left;
  queued--;
  poppedTotal++;
  if (used == 0) headPos = tailPos = 0;          // empty: restart at 0 for the most contiguous space
  return true;
}

// Copies `s` into `dst` without its leading and trailing line breaks. Returns the length, 0 if nothing is left.
size_t trimInto(char* dst, const char* s) {
  while (*s == '\n' || *s == '\r') s++;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) n--;
  if (n == 0) return 0;
  if (n > (size_t)LINE_CHARS - 1) n = LINE_CHARS - 1;
  memcpy(dst, s, n);
  dst[n] = 0;
  return n;
}

void enqueue(LogLevel level, uint8_t kind, const char* func, const char* text) {
  char t[LINE_CHARS];
  size_t n = trimInto(t, text);
  if (n == 0) return;
  Rec r;
  r.ms = millis();
  r.func = func ? func : "";
  r.len = (uint16_t)(n + 1);
  r.level = (uint8_t)level;
  r.kind = kind;
  uint32_t sz = recSize(r.len);
  lock();
  for (;;) {
    uint32_t toEnd = RING_BYTES - tailPos;
    uint32_t pad = toEnd < sz ? toEnd : 0;
    if (used + pad + sz <= RING_BYTES) {
      if (pad) {
        if (toEnd >= sizeof(Rec)) { Rec s = { 0, nullptr, SKIP, 0, 0 }; memcpy(ring + tailPos, &s, sizeof s); }
        used += pad;
        tailPos = 0;
      }
      break;
    }
    if (!popOldest()) continue;                  // emptied: try again from 0
    dropped++;
    droppedTotal++;
  }
  memcpy(ring + tailPos, &r, sizeof r);
  memcpy(ring + tailPos + sizeof(Rec), t, n + 1);
  tailPos += sz;
  if (tailPos >= RING_BYTES) tailPos = 0;
  used += sz;
  queued++;
  unlock();
}

void vlog(LogLevel level, uint8_t kind, const char* func, bool toLoki, const char* fmt, va_list ap) {
  char buf[384];
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  if (n < 0) return;
  if (n >= (int)sizeof buf) n = sizeof buf - 1;
  Serial.print(buf);
  if (n == 0 || buf[n - 1] != '\n') Serial.print("\n");
  if (toLoki) enqueue(level, kind, func, buf);
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

void logMsgAt(LogLevel level, const char* func, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(level, 0, func, true, fmt, ap);
  va_end(ap);
}

void logHeartbeatAt(const char* func, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(LOGLVL_INFO, 1, func, true, fmt, ap);
  va_end(ap);
}

void logLocal(LogLevel level, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vlog(level, 0, nullptr, false, fmt, ap);
  va_end(ap);
}

int lokiPending() {
  lock();
  int n = queued;
  unlock();
  return n;
}

LokiStats lokiStats() {
  lock();
  LokiStats s = { queued, droppedTotal, sentTotal, postFailuresTotal };
  unlock();
  return s;
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
    enqueue(LOGLVL_WARN, 0, "lokiFlush", m);
  }

  int pending = lokiPending();
  if (pending == 0) return 0;
  if (pending < SEND_NOW_LINES && everSent && (uint32_t)(nowMs - lastSendMs) < MIN_SEND_GAP_MS) return 0;

  // Build the body under the lock (no network in here).
  size_t cap = 12288;
  char* body = (char*)malloc(cap);
  if (!body) { cap = 4096; body = (char*)malloc(cap); }
  if (!body) { logLocal(LOGLVL_WARN, "loki: out of memory building a batch\n"); return -1; }
  Out o = { body, cap, 0, false };
  int count = 0;
  uint32_t firstSeq;
  lock();
  firstSeq = poppedTotal;
  {                                                          // how many records fit: a worst-case size estimate each
    Cursor c = { headPos, used };
    Rec r;
    size_t est = 1000;                                       // stream headers and framing
    while (count < BATCH_MAX_LINES && readNext(c, r, nullptr)) {
      size_t e = 2 * (size_t)r.len + 150;                    // every character escaped, plus timestamp, func and punctuation
      if (est + e > cap) break;
      est += e;
      count++;
    }
  }
  o.puts("{\"streams\":[");
  bool firstStream = true;
  for (int kind = 0; kind < NUM_KINDS; kind++) {
    for (int lvl = LOGLVL_DEBUG; lvl <= LOGLVL_ERROR; lvl++) {
      bool opened = false;
      Cursor c = { headPos, used };
      for (int i = 0; i < count; i++) {
        Rec r;
        const char* text;
        if (!readNext(c, r, &text)) break;
        if (r.level != lvl || r.kind != kind) continue;
        if (!opened) {
          if (!firstStream) o.put(',');
          o.puts("{\"stream\":{\"app\":\"street-cleaning-reveaLED\",\"kind\":\"");
          o.puts(KIND_NAMES[kind]);
          o.puts("\",\"level\":\"");
          o.puts(LEVEL_NAMES[lvl]);
          o.puts("\"},\"values\":[");
          opened = true;
          firstStream = false;
        } else {
          o.put(',');
        }
        uint64_t ageNs = (uint64_t)(uint32_t)(nowMs - r.ms) * 1000000ULL;      // how long ago it was logged
        uint64_t ts = ageNs <= nowNs ? nowNs - ageNs : nowNs;
        char tsStr[24];
        snprintf(tsStr, sizeof tsStr, "%llu", (unsigned long long)ts);
        o.puts("[\""); o.puts(tsStr); o.puts("\",\"");
#if LOKI_FUNC_MODE == 1
        if (r.func[0]) { o.puts("func="); o.str(r.func); o.put(' '); }
        o.str(text); o.puts("\"]");
#else
        o.str(text); o.puts("\"");
        if (r.func[0]) { o.puts(",{\"func\":\""); o.str(r.func); o.puts("\"}"); }
        o.put(']');
#endif
      }
      if (opened) o.puts("]}");
    }
  }
  o.puts("]}");
  unlock();
  if (count == 0 || o.over) {                                // cannot happen with these sizes; never send a cut-off body
    free(body);
    if (o.over) logLocal(LOGLVL_WARN, "loki: batch too large, dropping it\n");
    lock(); for (int i = 0; i < count && popOldest(); i++) {} unlock();
    return -1;
  }
  body[o.len] = 0;

  // Removes the sent records, unless overflow already dropped some of them meanwhile.
  auto removeSent = [&]() {
    lock();
    int32_t toPop = (int32_t)(firstSeq + (uint32_t)count - poppedTotal);
    for (int32_t i = 0; i < toPop && popOldest(); i++) {}
    unlock();
  };

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
    if (code >= 400 && code < 500) {                         // what Loki said about it, to diagnose a refusal
      String why = http.getString();
      why.replace('\n', ' ');
      logLocal(LOGLVL_WARN, "loki: refused: %.160s\n", why.c_str());
    }
    http.end();
  }
  free(body);
  lastSendMs = millis();

  if (code >= 200 && code < 300) {
    removeSent();
    lock(); sentTotal += (uint32_t)count; unlock();
    failures = 0;
    everSent = true;
    return count;
  }
  // A 400 / 413 / 422 means this batch itself is refused (for example a timestamp too old), and retrying it
  // would only block everything behind it: drop it. Anything else (no connection, 401, 429, 5xx) may pass later.
  bool refused = code == 400 || code == 413 || code == 422;
  if (refused) removeSent();
  lock(); postFailuresTotal++; unlock();
  uint32_t wait = BACKOFF_MS[failures < 5 ? failures : 4];
  if (failures < 1000) failures++;
  nextSendMs = millis() + wait;
  logLocal(LOGLVL_WARN, "loki: POST failed (http %d)%s; trying again in %lu s\n", code,
           refused ? ", batch dropped" : "", (unsigned long)(wait / 1000UL));
  return -1;
}
#endif  // ARDUINO
