// See weather-feed.h.
#include "weather-feed.h"

#ifdef ARDUINO

bool HttpsForecastSource::open(const char* url, const char* userAgent, uint32_t timeoutMs, const char* rootCaPem,
                               const char* accept) {
  close();
  timeoutMs_ = timeoutMs;
  total_ = 0; headLen_ = 0; head_[0] = 0;
  if (rootCaPem) client_.setCACert(rootCaPem);
  else client_.setInsecure();
  client_.setTimeout((timeoutMs + 999) / 1000);        // WiFiClientSecure takes seconds
  http_.setUserAgent(userAgent);
  http_.setConnectTimeout(timeoutMs);
  http_.setTimeout(timeoutMs);
  http_.useHTTP10(true);                                // no chunked transfer encoding
  http_.setReuse(false);
  http_.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (accept && *accept) http_.addHeader("Accept", accept);
  if (!http_.begin(client_, url)) { status_ = -1; return false; }
  status_ = http_.GET();
  if (status_ != HTTP_CODE_OK) { http_.end(); return false; }
  stream_ = http_.getStreamPtr();
  stream_->setTimeout(timeoutMs);
  return true;
}

void HttpsForecastSource::close() {
  if (stream_ || status_ > 0) http_.end();
  stream_ = nullptr;
}

int HttpsForecastSource::read() {
  char c;
  return readBytes(&c, 1) == 1 ? (unsigned char)c : -1;
}

// Reads exactly n bytes unless the server closes the connection or nothing arrives for the timeout.
// This must wait: the ESP32's TLS client returns whatever is buffered (often 0) from readBytes()
// rather than waiting, and the parser reads an early short return as the end of the document.
size_t HttpsForecastSource::readBytes(char* buf, size_t n) {
  if (!stream_) return 0;
  size_t got = 0;
  uint32_t last = millis();
  while (got < n) {
    int avail = client_.available();
    if (avail > 0) {
      size_t want = n - got;
      if ((size_t)avail < want) want = (size_t)avail;
      int r = client_.read((uint8_t*)buf + got, want);
      if (r > 0) {
        for (int i = 0; i < r && headLen_ < sizeof head_ - 1; i++) head_[headLen_++] = buf[got + i];
        head_[headLen_] = 0;
        got += (size_t)r;
        last = millis();
        continue;
      }
    } else if (!client_.connected()) {
      break;                                   // closed by the server and everything has been read
    }
    if (millis() - last >= timeoutMs_) break;  // nothing arrived for the whole timeout
    delay(1);
  }
  total_ += got;
  return got;
}

#endif  // ARDUINO
