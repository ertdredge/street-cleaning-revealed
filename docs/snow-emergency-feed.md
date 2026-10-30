# Street Cleaning ReveaLED — Snow Emergency Feed

---

## 1. Purpose and scope

When the City of Somerville declares a parking snow emergency, the display should use the actual start of the parking ban instead of the forecast start of the snow (Ambient-Display-Encoding.md, 1.2). Three values are needed:

| Value | Used for |
|---|---|
| Whether an emergency is declared, in effect, or neither | Whether the actual ban replaces the forecast in the snow removal risk |
| Start of the parking ban | $remaining$ in the snow removal risk (hours until the ban starts; 0 once it has started) |
| End of the parking ban, if announced | When the emergency stops counting |

The city's notices also say which side of the street stays open ("move to the ODD side"). That is not read. The side follows a fixed yearly alternation that is already in the design (`snowRiskOnRightBar`, Ambient-Display-Encoding.md 1.2.1).

Like the weather module, this module only reports. It does not decide anything about the display.

---

## 2. Source

### 2.1 Candidates

Evaluated on 2026-10-30.

| Candidate | Finding |
|---|---|
| Everbridge developer API (the city's alert vendor) | For the organization that sends the alerts only. Credentials come from Everbridge's Manager Portal and need an "API User" role inside a customer organization. Subscribers have no API. |
| Everbridge CAP RSS feed | Everbridge can publish a notification to a CAP RSS feed, but the sending organization enables it per notification. Somerville publishes none. |
| City news RSS (`https://www.somervillema.gov/news.xml`) | RSS 2.0, no key. Both the declaration and the end of an emergency are posted as news items, though not every time (3.2). |
| City Alerts email (Everbridge) into a dedicated mailbox | The most complete and the fastest record (3.2), but it needs a mailbox, mail access from the device or a relay, and the messages are written for people. |
| City pages (`/snow`, `/snowclosures`, `/alert`) | No machine-readable status. `/snowclosures` was still showing last winter's text on 2026-10-30. |
| Facebook, Instagram, Bluesky | Posts on every subject. Facebook and Instagram need bot credentials. Bluesky public posts can reportedly be read without credentials, but this was not tested. |
| 311 phone line, streetlight beacons, cable TV, radio | Not machine-readable. |

### 2.2 Decision

**The city news RSS feed is the sole source**, parsed on the ESP32, with no server-side relay. The headlines follow a regular pattern (3.2), the feed is small, and the logic stays on one device. A relay (for example a Google Apps Script web app returning JSON) would let the parser be fixed without reflashing. It was rejected to avoid a second system to run.

### 2.3 Request

One request per poll:

```
GET https://www.somervillema.gov/news.xml
```

- The same HTTPS byte source as the weather feed (`HttpsForecastSource`, weather-feed.h), with `Accept: application/rss+xml` passed as its `accept` argument.
- `User-Agent` as for NWS (the project name and a contact).
- HTTP/1.0, so the body is not chunked. No `Accept-Encoding`, so the body arrives uncompressed and is streamed straight into the scanner.
- Redirects are followed. TLS verification is off by default, as for NWS.

---

## 3. Feed structure

### 3.1 Fields

RSS 2.0, newest item first, **10 items** (about 8 days of news in October 2026). The live feed was 8,477 bytes on 2026-10-30.

| Field | Content | Used |
|---|---|---|
| `title` | The headline | Yes: everything is read from it |
| `pubDate` | `Thursday, October 29, 2026`: a date with no time of day, and not RFC 822 | Yes: gives the year when the title has none, and resolves "Sunday", "today", "tomorrow" |
| `link`, `guid` | The article URL | Kept for logging |
| `description` | A teaser, often cut off mid-sentence, sometimes empty | No |
| `dc:creator` | Always "No" | No |
| `category` | Absent | — |

The article body has the full detail, but no machine-readable dates (no `<time>` elements or date meta tags). Only the headline is used.

### 3.2 Coverage and wording, 2024–2026

City Alerts email subjects, with the news headline where one was posted:

| Emergency | Email subject | News headline |
|---|---|---|
| Jan 2024, declared | Snow Emergency Declared for 4 p.m. on Saturday, January 6, 2024 | none |
| Jan 2024, ended | Snow Emergency Will Be Lifted at 4 p.m, Sunday, January 7 | none |
| Feb 2024, declared | Snow Emergency in Effect Starting 7 p.m., Monday, Feb 12, 2024 | none |
| Feb 2024, ended | Somerville Snow Emergency Lifted Tuesday, February 13, at 10 a.m. | SOMERVILLE SNOW EMERGENCY ENDS Tuesday, Feb. 13, at 10 a.m. MOVE CARS FROM CITY LOTS BY 12 NOON |
| Feb 2025, declared | Snow Emergency Declared for Saturday, February 8, 2025, 5 p.m. | none |
| Feb 2025, ended | Snow Emergency Lifted at 12 p.m. noon, Sunday, February 9 | Snow Emergency Ends Feb. 9 at Noon; Move Cars From City Lots by 2 p.m. Sunday. |
| Jan 2026, declared | Snow Emergency Declared for 7 a.m. on Sunday, January 25, 2026 | Somerville Declares Snow Emergency Effective Sunday, January 25, at 7 a.m. |
| Jan 2026, ended | Snow Emergency will end at 12 p.m. (noon), Tuesday, January 27 | Somerville Snow Emergency Will End Tuesday, January 27, at 12 Noon |
| Feb 2026, declared | Snow Emergency Declared for 12 p.m. (noon) on Sunday, February 22, 2026 | Somerville Declares Snow Emergency Effective Sunday, February 22, at 12 p.m. Noon |
| Feb 2026, ended | Snow Emergency Will End at 8 p.m., Tuesday, February 24 | Somerville Snow Emergency Will End Tuesday, February 24 at 8 p.m. |

- Four of the ten alerts had no news item, all before February 9, 2025. The five alerts since then all had one.
- The declaration and the end are separate items. Each headline names one time, usually with a date. The year is often missing.
- These five emergencies lasted 15 to 56 hours.
- Older headlines (The Somerville Times reprints, 2013–2019) use more varied wording ("to lift at 4 p.m. Monday, March 4", "lifting at noon Wed. 1/28", "lifted as of 11 a.m. Sunday, Dec. 15").

---

## 4. Derived values

All times are local (US Eastern, the TZ rule the sketch sets), as for the weather feed.

### 4.1 Normalizing a headline

Everything after the first `;` is ignored ("…at Noon; Move Cars From City Lots by 2 p.m."). The title is lowercased. `.` and `'` are dropped ("p.m." becomes "pm", "Feb." becomes "feb"). Letters and digits are split ("4pm" becomes "4 pm"). A run of digits keeps an inner `:` or `/` ("9:30", "1/28"). Any other punctuation separates words.

### 4.2 Kind

A headline is a notice only if it contains the words **snow emergency** (singular, so "Snow emergencies: what to know" is not one). The first keyword then decides:

| Kind | Keywords |
|---|---|
| Declared | declare, declares, declared, declaring, effective, starting, starts, begins, beginning, issued, *in effect* |
| Ended | lift, lifts, lifted, lifting, end, ends, ended, ending, cancel(s/ed/led), rescinded, expire(s/d), *called off*, *no longer* |

A headline that names neither is not a notice ("Snow Emergency Parking Exceptions").

### 4.3 Time

The first time in the headline: `H am|pm` or `H:MM am|pm` (hour 1–12), `12 noon`, `noon`, `12 midnight`, `midnight`. "12 p.m. (noon)" reads as 12 p.m. **Midnight on day D means the end of D**, the AP convention (00:00 of the next day). The city has not used midnight yet, so this is an assumption.

### 4.4 Date

The first of: a month name and day number, with an optional 4-digit year ("Feb. 13", "January 6, 2024"); a day number and month name ("13 February 2024"); or `m/d`, `m/d/yy` or `m/d/yyyy`. Without any of these: a weekday name (the first such day on or after publication), "tomorrow", or "today"/"tonight" (the publication day).

**Year.** When the headline has none, the year is the one that puts the date nearest the publication date, within 200 days. A year whose weekday matches a weekday named in the headline is preferred. So "Friday, January 1", published December 31, 2026, is 2027.

### 4.5 Timed and untimed notices

A notice with both a date and a time is **timed**. Without them it is **untimed**: the tracker ignores untimed declarations ("Somerville declares snow emergency Monday") and treats an untimed end as "ended when first seen" (5.3).

### 4.6 Known limitation

An explainer headline that contains a keyword is recognized as a notice of that kind. The synthetic fixture "What Happens When a Snow Emergency Is Declared?" is an untimed declaration. It changes nothing (untimed declarations are ignored), but it is counted as a notice. A possible rule: a headline with `?`, or with *when, if, what, how, why, before, after* immediately before "snow emergency", is not a notice. Not implemented.

---

## 5. Polling and failure handling

### 5.1 Cadence

**Hourly.** After a declaration residents have four hours to move their cars, so an hour's delay is acceptable. The feed covers about 8 days, so an hourly poll cannot miss an item.

### 5.2 Outcomes

The module itself only scans and interprets. The sketch decides on each fetch:

| Outcome | Test | Action |
|---|---|---|
| Fetch failed | `HttpsForecastSource::open()` returned false (`httpStatus()` says why) | Keep the state; retry sooner |
| Not a feed | `RssScanner::sawFeed()` is false (an HTML error page, an empty body) | Keep the state; retry sooner |
| Feed read | `sawFeed()` is true | `SnowEmergency::update()`; save the state if it changed |

A feed with no snow notices is a normal outcome. It never clears an emergency: only notices and time do.

### 5.3 State and episodes

The state is two times: the **start** of the parking ban, and its **end** (0 if not announced). On every poll the whole feed is replayed, oldest item first, so applying the same feed twice changes nothing.

| Notice | Rule |
|---|---|
| Timed declaration within 48 h of the current start, before its announced end | Revises the start |
| Any other timed declaration later than the current start | A new emergency: that start, no end |
| Timed end after the start (within 30 days, or within 48 h of an end already announced) | Sets or revises the end |
| Timed end still in the future, with no open emergency to attach it to | The declaration was missed (February 2024): the emergency is taken to have begun at the start of the publication day, ending at the announced time |
| Untimed end, published on or after the start day, while no end is known | The emergency ends when the notice is first seen |
| Untimed declaration | Ignored |
| Older notices (the previous emergency) | Ignored |

**Phase**, at time *now*: *none* if there is no start, the end has passed, or there is no end and `SNOW_EMERGENCY_MAX_OPEN_HOURS` (96) have passed since the start; otherwise *declared* before the start and *in effect* from the start.

The 96-hour limit guards against a missing end notice, as in January 2024. The longest of the five emergencies in 3.2 lasted 56 hours.

### 5.4 Persistence

The declaration scrolls out of the 10-item feed within days, so a reboot during an emergency would lose it. The state (two 64-bit times) must be saved to NVS (`Preferences`) whenever it changes and restored at boot. The sketch does this (`saveEmergencyState` / `restoreEmergencyState`, namespace `snowem`).

---

## 6. Architecture

### 6.1 Parts

The same split as the weather feed (weather-feed.md, 6).

| Part | Files | Depends on |
|---|---|---|
| Headline parser, RSS scanner, emergency tracker | `snow-emergency.h/.cpp` | The C library only; no Arduino headers, no ArduinoJson |
| ESP32 fetch layer | `weather-feed.h/.cpp` (`HttpsForecastSource`, shared with the weather feed) | Arduino HTTP and WiFi libraries |
| Display logic | The sketch (not wired yet) | Both, and `snow-risk.h` |

- **The scanner is push-based.** Bytes are pushed in any number at a time and only each item's title, date and link are kept (`RssScanner`, about 7 KB with 16 items). The body is never held in memory. CDATA sections, comments and the usual entities (`&amp;`, `&#039;`, `&#8217;`, double-encoded `&amp;nbsp;`) are handled.
- **Time is injected.** Every call takes `now` (or the publication date) as an argument.
- **No regex library.** `std::regex` is large and recursive (the Arduino loop task has an 8 KB stack), and the token rules in 4.1–4.4 are simpler to get right by hand.

### 6.2 Interface

```cpp
// One headline
enum SnowNoticeKind { SNOW_NOTICE_NONE, SNOW_NOTICE_DECLARED, SNOW_NOTICE_ENDED };
struct SnowNotice { SnowNoticeKind kind; bool timed; time_t when; };
SnowNotice parseSnowNotice(const char* title, time_t published);   // published: any time that day, or 0
time_t     parseFeedDate(const char* pubDate);                      // local noon of that day, or 0

// RSS items, streamed
struct RssItem { char title[160]; char pubDate[48]; char link[200]; };
class RssScanner {                                   // MAX_ITEMS = 16, feed order (newest first)
  void reset();
  void push(char c);  void push(const char* buf, size_t n);
  int count() const;  const RssItem& item(int i) const;
  bool sawFeed() const;  int dropped() const;  size_t bytes() const;
};
template <class Source> size_t scanRss(Source& src, RssScanner& rss);   // any readBytes() source, to the end

// The emergency
const long SNOW_EMERGENCY_MAX_OPEN_HOURS = 96;
struct SnowEmergencyState { time_t start; time_t end; };            // 0 = unknown / not announced
enum SnowEmergencyPhase { SNOW_EMERGENCY_NONE, SNOW_EMERGENCY_DECLARED, SNOW_EMERGENCY_IN_EFFECT };
class SnowEmergency {
  SnowEmergencyState state;                                         // public: save and restore it
  int  update(const RssScanner& rss, time_t now);                   // replays the feed; returns notices seen
  void apply(const SnowNotice& n, time_t published, time_t now);    // one notice (tests, serial overrides)
  SnowEmergencyPhase phase(time_t now) const;
};
const char* snowNoticeKindName(SnowNoticeKind);  const char* snowEmergencyPhaseName(SnowEmergencyPhase);
```

`HttpsForecastSource::open(url, userAgent, timeoutMs = 20000, rootCaPem = nullptr, accept = "application/geo+json")`: the snow emergency fetch passes `"application/rss+xml"` as `accept`.

### 6.3 Wiring into the sketch (done 2026-10-30; not yet compiled for the ESP32)

The intended shape, following the weather task (`weatherTask` in `street-cleaning-revealed.ino`):

```cpp
#include "snow-emergency.h"
const char* const SNOW_URL = "https://www.somervillema.gov/news.xml";
SnowEmergency emergency;              // guarded by a mutex: written on core 0, read by the renderer on core 1
static RssScanner rss;                // about 7 KB: static, not on the task stack

void pollSnowEmergency() {            // hourly, after WiFi and the clock; on core 0, one TLS session at a time
  HttpsForecastSource feed;
  if (!feed.open(SNOW_URL, NWS_USER_AGENT, 20000, nullptr, "application/rss+xml")) return;   // keep the state
  rss.reset();
  scanRss(feed, rss);
  feed.close();
  if (!rss.sawFeed()) return;                                   // an error page: keep the state
  SnowEmergency next = emergency;                               // (copied under the mutex)
  next.update(rss, realNow());
  // if next.state differs: store it under the mutex and save start/end to Preferences
}
```

In the risk calculation (`currentSnowEvents` / `updateFromClock`), while `emergency.phase(realNow())` is not `SNOW_EMERGENCY_NONE`, the actual ban is a snow event at full size:

```cpp
SnowEvent e = { true, SNOW_EMERGENCY_INCHES, 100, emergency.state.start };
```

`snowRiskFrom` then gives the street cleaning risk for the hours until the ban starts, and 90 once it has started (negative $remaining$ clamps to 90). This is the rule already written in Ambient-Display-Encoding.md 1.2. Open questions for the wiring:

- **After an announced end.** The forecast-based snow event can keep the bar at 90 until local midnight (Ambient-Display-Encoding.md 1.2.1). Should a known end suppress forecast events that started before it?
- **Simulated clock.** Like the real forecast, the real emergency should probably be left out while the clock is simulated, with a serial override (`emergency …|off`) for testing, mirroring `snow P IN H|off`.
- **Serial observability.** An `emergency` command printing the state, phase and last fetch outcome.

---

## 7. Test strategy

- **Host-tested without hardware** (`test/snow-emergency-test.cpp`, part of `make test`): 119 checks.
- **Every real headline.** All ten email subjects and the six news headlines in 3.2, the Somerville Times wordings, and further forms the grammar accepts (across New Year, midnight, "tonight", "tomorrow", a bare weekday, `m/d/yy`, a full date with no publication date).
- **Non-notices.** Unrelated news, parking-rule and explainer headlines, and the plural "snow emergencies".
- **Scanner.** Synthetic feeds in `test/data/snow-emergency/` with the structure of the real feed: the 2026-10-29 news, the January 2026 declaration, the January 2026 end, and an encoding torture file (CDATA, `]]` inside CDATA, entities, a commented-out item, an empty `<item/>`, more than 16 items). Byte-at-a-time and block input give the same result. An HTML error page and an empty body are "not a feed".
- **Episodes.** January 2026 as the feed told it, replay idempotence, a device that boots after the end was posted, a revised start, a revised end, a new emergency while old items are still in the feed, the missed February 2024 declaration (seen before and after its end), an emergency stuck open past 96 hours, and untimed ends.
- **Live feed.** `curl -s https://www.somervillema.gov/news.xml | ./snow-emergency-test -` lists each item with its kind and time, and the resulting state. On 2026-10-30 it read all 10 items with no notices.
- **Still wanted:** a raw capture of the feed during a real emergency, saved as a fixture.

---

## 8. Considered and not used

| Item | Reason |
|---|---|
| Everbridge API, Everbridge CAP RSS | Not available to a subscriber (2.1) |
| Email alerts through a dedicated mailbox | More complete and faster (3.2), but a mailbox and mail access to maintain. A latency and coverage upgrade if missed notices prove to matter. |
| A relay that parses on a server and serves JSON | Section 2.2 |
| Reading the article body | No machine-readable dates; the headline carries the start or end time |
| The `description` teaser | Truncated, sometimes empty, and adds nothing the headline lacks |
| `std::regex` or POSIX `regex.h` | Section 6.1 |
| Push notifications to the device | The source has to be polled anyway, and the four-hour window makes hourly enough |
