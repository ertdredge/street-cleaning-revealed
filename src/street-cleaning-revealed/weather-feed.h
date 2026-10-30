// Live HTTPS fetch of the NWS gridpoint feed for the ESP32. This is the only weather file that
// touches the network; everything else (weather-forecast.h/.cpp) is plain C++ tested on a Mac.
// The whole file compiles to nothing off the Arduino, so the host build is unaffected.
//
//   HttpsForecastSource feed;
//   feed.open("https://api.weather.gov/gridpoints/BOX/70,102", "street-cleaning-revealed/1.0 (you@example.com)");
//   DechunkingSource body(feed);
//   forecast.update(body, time(nullptr));          // forecast is a WeatherForecast
//   feed.close();
//
// Notes on the choices made here:
//   * HTTP/1.0 is requested so the server doesn't use chunked transfer encoding, whose chunk-size
//     lines would otherwise appear in the middle of the JSON and look like a corrupt feed. If a
//     server chunks anyway, wrap this source in a DechunkingSource (weather-forecast.h).
//   * No Accept-Encoding header is sent, so the (about 290 KB) body arrives uncompressed and is
//     streamed straight into the parser; it is never held in memory.
//   * NWS asks every client to send a User-Agent that identifies the application and a contact.
//   * Redirects are followed (NWS redirects when a grid cell is renumbered).
//   * TLS: by default the server certificate is NOT verified (setInsecure), which is acceptable for
//     a public read-only forecast driving an art installation, but you can pin a root CA instead:
//     pass a PEM string as `rootCaPem`.
//   * `accept` is the Accept header: GeoJSON for NWS by default; the snow emergency feed passes
//     "application/rss+xml" (snow-emergency.h). The class is a plain HTTPS byte stream either way.
//   * open() returns false on a connection failure or any HTTP status other than 200; httpStatus()
//     tells which (negative values are HTTPClient transport errors). A source that failed to open
//     just reads as empty, so passing it to update() safely yields FORECAST_IO_ERROR.
//   * Run it no more often than hourly, and only after WiFi is connected and the clock is set.
//   * This file has not been compiled in this repository yet; build the sketch with arduino-cli.
#pragma once
#ifdef ARDUINO
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "weather-forecast.h"

class HttpsForecastSource : public ForecastSource {
 public:
  HttpsForecastSource() : stream_(nullptr), status_(0), total_(0), headLen_(0), timeoutMs_(20000) { head_[0] = 0; }
  ~HttpsForecastSource() { close(); }

  bool open(const char* url, const char* userAgent, uint32_t timeoutMs = 20000, const char* rootCaPem = nullptr,
            const char* accept = "application/geo+json");
  void close();
  int httpStatus() const { return status_; }
  size_t bytesRead() const { return total_; }                 // raw bytes received from the server (since open())
  const char* head() const { return head_; }                  // the first raw bytes received, NUL-terminated, for diagnosing a bad body

  int read() override;
  size_t readBytes(char* buf, size_t n) override;

 private:
  HttpsForecastSource(const HttpsForecastSource&);
  HttpsForecastSource& operator=(const HttpsForecastSource&);
  WiFiClientSecure client_;
  HTTPClient http_;
  Stream* stream_;
  int status_;
  size_t total_;
  size_t headLen_;
  uint32_t timeoutMs_;
  char head_[161];
};
#endif  // ARDUINO
