// See weather-feed.h.
#include "weather-feed.h"

#ifdef ARDUINO

bool HttpsForecastSource::open(const char* url, const char* userAgent, uint32_t timeoutMs, const char* rootCaPem) {
  close();
  if (rootCaPem) client_.setCACert(rootCaPem);
  else client_.setInsecure();
  client_.setTimeout((timeoutMs + 999) / 1000);        // WiFiClientSecure takes seconds
  http_.setUserAgent(userAgent);
  http_.setConnectTimeout(timeoutMs);
  http_.setTimeout(timeoutMs);
  http_.useHTTP10(true);                                // no chunked transfer encoding
  http_.setReuse(false);
  http_.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http_.addHeader("Accept", "application/geo+json");
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

size_t HttpsForecastSource::readBytes(char* buf, size_t n) {
  return stream_ ? stream_->readBytes(buf, n) : 0;      // waits up to the timeout for data
}

#endif  // ARDUINO
