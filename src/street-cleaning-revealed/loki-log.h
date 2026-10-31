// Logging to the serial port and to Grafana Cloud Loki (ESP32 only).
//
//   logMsg(LOGLVL_INFO, "weather: fetching\n");        printf-style, like Serial.printf
//
// logMsg prints to Serial immediately and then queues the line for Loki. It never touches the network and
// never waits for it, so serial interaction and the display are not held up. The queue is a small ring
// (LOKI_QUEUE_LINES); when it fills, the oldest lines are dropped from the Loki queue (never from Serial) and
// a warning line says how many.
//
// Lines go out in batches from lokiFlush(), which does the blocking HTTPS POST and so must be called from a
// task that may block (the weather task, on core 0). A line is only sent once the clock is valid (NTP has
// succeeded), stamped with the time it was logged: lines logged before that wait in the queue and get
// their timestamps worked out afterwards from how long ago they were logged. Loki's push API requires a
// timestamp on every line, so a line cannot be sent without one. If NTP never succeeds nothing is sent.
//
// secrets.h supplies LOKI_URL (the base URL, e.g. https://logs-prod-xxx.grafana.net), LOKI_USER and
// LOKI_TOKEN. The stream labels are app="street-cleaning-reveaLED" and level=debug|info|warn|error.
//
// Security note: like the NWS fetches, the TLS connection does not verify the server certificate
// (setInsecure), so the Loki token could be captured by someone impersonating the server on your network.
// Use a token that can only write logs, and see the TODO in loki-log.cpp to pin a root CA.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <stdint.h>

enum LogLevel { LOGLVL_DEBUG = 0, LOGLVL_INFO, LOGLVL_WARN, LOGLVL_ERROR };

void logBegin();   // call first in setup(), before any logMsg

// Serial, then the Loki queue.
void logMsg(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// Serial only: for diagnostics about logging itself, which must not feed back into the queue.
void logLocal(LogLevel level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Sends queued lines to Loki, a batch at a time. `nowNs` is the current real time in nanoseconds since the
// Unix epoch, or 0 when the clock is not valid yet (then nothing is sent). May block for several seconds.
// Returns the number of lines sent, 0 if nothing was due, or -1 if the attempt failed.
int lokiFlush(uint64_t nowNs);

int lokiPending();   // lines waiting
#endif  // ARDUINO
