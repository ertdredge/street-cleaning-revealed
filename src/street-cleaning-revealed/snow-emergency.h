// City of Somerville parking snow emergencies, read from the city's news RSS feed
// (https://www.somervillema.gov/news.xml). Pure parsing and bookkeeping: no Arduino.h, no WiFi, so it
// builds and is tested on a Mac (`make test` in test/). On the ESP32 the feed is fetched with
// HttpsForecastSource (weather-feed.h), which despite its name is just an HTTPS byte stream.
//
// ---- Why the news feed ---------------------------------------------------------------------------
// The city publishes no machine-readable status. Its Everbridge API is for the sending organization
// only. The news feed is RSS 2.0 with the 10 newest items (title, link, pubDate "Thursday, October 29,
// 2026", a teaser; no categories, no time of day), newest first. Snow emergencies appear as one item
// when declared and another when they end. Not every alert is posted (in 2024 three of the four
// emailed alerts had no news item), so a missed declaration or end is expected and handled below.
//
// ---- The headline grammar ---------------------------------------------------------------------------
// Only the title is read. Everything after the first ';' is ignored ("...at Noon; Move Cars From City
// Lots by 2 p.m."). The title is lowercased, '.' and '\'' are dropped, digits and letters are split
// ("4pm" -> "4 pm"), and other punctuation separates words. Then, roughly in regex terms:
//
//   subject   snow emergency                                     (required)
//   declared  declare[sd]?|declaring|effective|starting|starts|begins|beginning|issued|in effect
//   ended     lift(s|ed|ing)?|end(s|ed|ing)?|cancel(s|led|ed)?|rescinded|expire[sd]?|called off|no longer
//             (the earlier of the two keywords decides; neither -> not a notice)
//   time      (\d{1,2})(:\d\d)? (am|pm)  |  12 noon  |  noon  |  12 midnight  |  midnight
//             (the first one in the title; "12 p.m. (noon)" is just 12 p.m.; midnight on day D means
//             the end of D, AP style)
//   date      month-name day [year]  |  day month-name [year]  |  m/d[/yy[yy]]
//             else a weekday name (the first such day on or after publication), "tomorrow", or
//             "today"/"tonight" (the publication day)
//   year      when absent: the year that puts the date nearest the publication date (within 200 days),
//             preferring one whose weekday matches a weekday named in the title
//
// Every headline the city has used for its 2024-2026 emergencies is in test/snow-emergency-test.cpp.
// No regex library is used: std::regex is large and recursive (the Arduino loop task has an 8 KB
// stack) and the tokens above are simpler to get right by hand.
//
// ---- Episodes ------------------------------------------------------------------------------------
// SnowEmergencyState is two times: the start of the parking ban and its end (0 = not announced). The
// feed is replayed oldest item first on every poll, so applying it again changes nothing.
//   * A declaration within 48 hours of the current start (and before its announced end) revises the
//     start; a later one begins a new emergency with no end.
//   * An end after the start (within 30 days, or within 48 hours of an end already announced) sets the
//     end. An end that names a time still in the future when there is no open emergency means the
//     declaration was missed: the emergency is taken to have begun at the start of the publication day.
//   * An end with no time ("Somerville's Snow Emergency Lifted") ends an open emergency when first
//     seen, if it was published on or after the start day.
//   * An emergency whose end is never announced is dropped SNOW_EMERGENCY_MAX_OPEN_HOURS after its start
//     (the 2024-2026 emergencies lasted 15 to 56 hours).
// Keep the state across reboots (Preferences, two 64-bit values): the declaration scrolls out of the
// 10-item feed within days.
//
// ---- Usage in the sketch (an outline, not compiled here) ----------------------------------------------
//   #include "snow-emergency.h"
//   #include "weather-feed.h"
//   SnowEmergency emergency;                     // global; restore emergency.state from Preferences at boot
//   const char* const SNOW_URL = "https://www.somervillema.gov/news.xml";
//
//   void pollSnowEmergency() {                   // hourly, once WiFi is up and the clock is set
//     static RssScanner rss;                     // about 7 KB: keep it off the task stack
//     HttpsForecastSource feed;
//     if (!feed.open(SNOW_URL, NWS_USER_AGENT, 20000, nullptr, "application/rss+xml")) return;
//     rss.reset();
//     scanRss(feed, rss);
//     feed.close();
//     if (!rss.sawFeed()) return;                // an error page, not the feed: keep the old state
//     emergency.update(rss, time(nullptr));      // then save emergency.state if it changed
//   }
//
//   // 1.2: an actual emergency replaces the forecast start. As a SnowEvent (snow-risk.h) at full size,
//   // remaining = hours until the ban starts, and 0 (risk 90) while it is in effect:
//   if (emergency.phase(now) != SNOW_EMERGENCY_NONE) {
//     SnowEvent e = { true, SNOW_EMERGENCY_INCHES, 100, emergency.state.start };
//     ...
//   }
#pragma once
#include <stddef.h>
#include <time.h>

// ---- One headline -------------------------------------------------------------------------------------
enum SnowNoticeKind { SNOW_NOTICE_NONE = 0, SNOW_NOTICE_DECLARED, SNOW_NOTICE_ENDED };

struct SnowNotice {
  SnowNoticeKind kind;   // NONE: not a snow emergency declaration or end
  bool   timed;          // a date and a clock time were found, so `when` is set
  time_t when;           // DECLARED: when the parking ban starts; ENDED: when it ends. 0 unless timed
};

// `published`: any time on the day the item was published (parseFeedDate of its pubDate), which supplies
// the year when the title has none and resolves "Sunday", "today" and "tomorrow". 0 if unknown, in which
// case only titles with a full date can be timed. Local time: TZ must be set.
SnowNotice parseSnowNotice(const char* title, time_t published);

// "Thursday, October 29, 2026" (the city's feed) or RFC 822 "Thu, 29 Oct 2026 14:05:00 -0400" (the time
// of day is ignored). Returns local noon of that day, or 0 if there is no recognizable date.
time_t parseFeedDate(const char* s);

const char* snowNoticeKindName(SnowNoticeKind k);    // "none", "declared", "ended"

// ---- RSS items, streamed ------------------------------------------------------------------------------
// Push the response body in, any number of bytes at a time; the title, pubDate and link of each <item>
// (Atom <entry> also works: title, published) are kept, in feed order (newest first), up to MAX_ITEMS.
// CDATA sections and the usual entities (&amp; &#039; &#8217; ...) are handled. Nothing else is stored,
// so the body is never held in memory. Over-long fields are truncated.
struct RssItem {
  char title[160];
  char pubDate[48];
  char link[200];
};

class RssScanner {
 public:
  static const int MAX_ITEMS = 16;
  RssScanner() { reset(); }
  void reset();
  void push(char c);
  void push(const char* buf, size_t n) { for (size_t i = 0; i < n; i++) push(buf[i]); }
  int count() const { return n_; }
  const RssItem& item(int i) const { return items_[i]; }
  bool sawFeed() const { return sawFeed_; }      // an <rss>, <channel> or <feed> tag was seen
  int dropped() const { return dropped_; }       // items beyond MAX_ITEMS
  size_t bytes() const { return bytes_; }

 private:
  enum State { TEXT, TAG, CDATA, COMMENT };
  void put(char c);
  void endTag();
  void capture(char* field, size_t size);
  void finishItem();
  RssItem items_[MAX_ITEMS];
  RssItem cur_;
  int n_, dropped_;
  State state_;
  char tag_[32];
  size_t tagLen_;
  char tagLast_;
  bool inItem_, sawFeed_;
  char* cap_;
  size_t capLen_, capMax_;
  int pend_;
  size_t bytes_;
};

// Reads `src` to the end into `rss` (which is not reset first). Works with any source that has
// readBytes(char*, size_t) returning 0 at the end: HttpsForecastSource, FileForecastSource.
template <class Source>
size_t scanRss(Source& src, RssScanner& rss) {
  char buf[128];
  size_t total = 0, n;
  while ((n = src.readBytes(buf, sizeof buf)) > 0) { rss.push(buf, n); total += n; }
  return total;
}

// ---- The emergency --------------------------------------------------------------------------------------
const long SNOW_EMERGENCY_MAX_OPEN_HOURS = 96;   // an emergency with no announced end is dropped after this

struct SnowEmergencyState {
  time_t start;   // when the parking ban starts or started; 0 = no emergency known
  time_t end;     // when it ends or ended; 0 = not announced
};

enum SnowEmergencyPhase {
  SNOW_EMERGENCY_NONE,        // none known, or it has ended
  SNOW_EMERGENCY_DECLARED,    // declared; the parking ban has not started yet
  SNOW_EMERGENCY_IN_EFFECT    // the parking ban is on (state.end, if set, says until when)
};

const char* snowEmergencyPhaseName(SnowEmergencyPhase p);   // "none", "declared", "in effect"

class SnowEmergency {
 public:
  SnowEmergency() { state.start = 0; state.end = 0; }
  SnowEmergencyState state;                    // public so the sketch can save and restore it

  // Applies every item of the feed, oldest first. Returns how many were snow emergency notices
  // (timed or not). `now` is only used for the two rules that depend on it (see Episodes above).
  int update(const RssScanner& rss, time_t now);
  void apply(const SnowNotice& n, time_t published, time_t now);
  SnowEmergencyPhase phase(time_t now) const;
};
