// See snow-emergency.h.
#include "snow-emergency.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const time_t HOUR = 3600;
const time_t DAY = 24 * HOUR;
const time_t SAME_EPISODE = 48 * HOUR;        // a re-announced start or end moves by less than this
const time_t MAX_EPISODE = 30 * DAY;          // an end further than this from the start is not its end
const time_t YEAR_WINDOW = 200 * DAY;         // an inferred year puts the date this close to publication

// ---- Words ----------------------------------------------------------------------------------------

struct Tok { char s[16]; };
const int MAX_TOKENS = 48;

bool isAsciiAlpha(unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isAsciiDigit(unsigned char c) { return c >= '0' && c <= '9'; }

// Lowercase words and numbers. '.' and '\'' vanish ("p.m." -> "pm", "Feb." -> "feb"); a digit run keeps
// an inner ':' or '/' ("9:30", "1/28"); letters and digits split ("4pm" -> "4", "pm"); anything else
// separates. Stops at ';' (or a sentence that follows it).
int tokenize(const char* in, Tok* t, int max, bool stopAtSemicolon) {
  int n = 0, cls = 0;                       // 0 between words, 1 letters, 2 digits
  size_t len = 0;
  char cur[16];
  for (const char* p = in;; p++) {
    unsigned char c = (unsigned char)*p;
    if (c == '.' || c == '\'') continue;
    int k = 0;
    if (c == 0 || (stopAtSemicolon && c == ';')) k = -1;
    else if (isAsciiAlpha(c)) k = 1;
    else if (isAsciiDigit(c)) k = 2;
    else if ((c == ':' || c == '/') && cls == 2 && isAsciiDigit((unsigned char)p[1])) k = 2;
    if (k != cls) {
      if (len > 0 && n < max) { memcpy(t[n].s, cur, len); t[n].s[len] = 0; n++; }
      len = 0;
    }
    if (k < 0) break;
    cls = k;
    if (k != 0 && len < sizeof cur - 1) cur[len++] = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
  }
  return n;
}

bool is(const Tok& t, const char* s) { return strcmp(t.s, s) == 0; }

bool isIn(const Tok& t, const char* const* list) {
  for (; *list; list++) if (is(t, *list)) return true;
  return false;
}

// A plain number (digits only); -1 otherwise.
long plainNumber(const Tok& t) {
  if (!t.s[0]) return -1;
  for (const char* p = t.s; *p; p++) if (!isAsciiDigit((unsigned char)*p)) return -1;
  return strtol(t.s, nullptr, 10);
}

// 1..12 for "jan", "january", "sept", ...; 0 otherwise.
int monthOf(const Tok& t) {
  static const char* const NAMES[12] = { "january", "february", "march", "april", "may", "june", "july",
                                         "august", "september", "october", "november", "december" };
  size_t len = strlen(t.s);
  if (len < 3) return 0;
  for (int m = 0; m < 12; m++) {
    size_t full = strlen(NAMES[m]);
    if (len > full || strncmp(NAMES[m], t.s, len) != 0) continue;
    if (len == 3 || len == full || (m == 8 && len == 4)) return m + 1;
  }
  return 0;
}

// 0..6 (Sunday = 0) for "sun", "tues", "thurs", "wednesday", ...; -1 otherwise.
int weekdayOf(const Tok& t) {
  static const char* const NAMES[7] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
  size_t len = strlen(t.s);
  if (len < 3) return -1;
  for (int d = 0; d < 7; d++) {
    size_t full = strlen(NAMES[d]);
    if (len <= full && strncmp(NAMES[d], t.s, len) == 0) return d;
  }
  return -1;
}

bool isOrdinalSuffix(const Tok& t) { return is(t, "st") || is(t, "nd") || is(t, "rd") || is(t, "th"); }

// ---- Calendar ---------------------------------------------------------------------------------------

time_t localTime(int y, int mo, int d, int h, int mi) {
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}

bool validDate(int y, int mo, int d) {
  if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
  struct tm t;
  memset(&t, 0, sizeof t);
  t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = 12; t.tm_isdst = -1;
  mktime(&t);
  return t.tm_year == y - 1900 && t.tm_mon == mo - 1 && t.tm_mday == d;   // not Feb 30
}

struct Ymd { int y, m, d; };

Ymd ymdOf(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  Ymd r = { lt.tm_year + 1900, lt.tm_mon + 1, lt.tm_mday };
  return r;
}

int weekdayAt(int y, int m, int d) {
  time_t t = localTime(y, m, d, 12, 0);
  struct tm lt;
  localtime_r(&t, &lt);
  return lt.tm_wday;
}

Ymd addDays(Ymd a, int days) { return ymdOf(localTime(a.y, a.m, a.d + days, 12, 0)); }

// ---- Pieces of a headline ---------------------------------------------------------------------------

struct Clock { bool found; int h, m; bool midnight; };

bool hourMinute(const char* s, int* h, int* m) {     // "4" or "9:30", hour 1..12
  char* end;
  long hh = strtol(s, &end, 10);
  if (end == s) return false;
  long mm = 0;
  if (*end == ':') {
    const char* q = end + 1;
    mm = strtol(q, &end, 10);
    if (end - q != 2 || mm > 59) return false;
  }
  if (*end || hh < 1 || hh > 12) return false;
  *h = (int)hh; *m = (int)mm;
  return true;
}

Clock findClock(const Tok* t, int n) {
  Clock none = { false, 0, 0, false };
  for (int i = 0; i < n; i++) {
    Clock c = none;
    c.found = true;
    if (is(t[i], "noon")) { c.h = 12; return c; }
    if (is(t[i], "midnight")) { c.midnight = true; return c; }
    int h, m;
    if (i + 1 < n && hourMinute(t[i].s, &h, &m)) {
      if (is(t[i + 1], "am")) { c.h = h % 12; c.m = m; return c; }
      if (is(t[i + 1], "pm")) { c.h = h % 12 + 12; c.m = m; return c; }
      if (h == 12 && m == 0 && is(t[i + 1], "noon")) { c.h = 12; return c; }
      if (h == 12 && m == 0 && is(t[i + 1], "midnight")) { c.midnight = true; return c; }
    }
  }
  return none;
}

struct DateHit { int y, m, d; };   // m == 0: none; y == 0: no year given

bool dayNumber(const Tok& t, int* d) {
  long v = plainNumber(t);
  if (v < 1 || v > 31) return false;
  *d = (int)v;
  return true;
}

int yearAfter(const Tok* t, int n, int j) {          // a 4-digit year at t[j], after any ordinal suffix
  if (j < n && isOrdinalSuffix(t[j])) j++;
  if (j >= n) return 0;
  long v = plainNumber(t[j]);
  return (strlen(t[j].s) == 4 && v >= 1970 && v < 2200) ? (int)v : 0;
}

DateHit findDate(const Tok* t, int n) {
  DateHit r = { 0, 0, 0 };
  for (int i = 0; i < n; i++) {
    int m = monthOf(t[i]), d;
    if (m && i + 1 < n && dayNumber(t[i + 1], &d)) {               // "feb 13", "january 6 2024"
      r.m = m; r.d = d; r.y = yearAfter(t, n, i + 2);
      return r;
    }
    if (m && i > 0 && dayNumber(t[i - 1], &d)) {                   // "13 february 2024"
      r.m = m; r.d = d; r.y = yearAfter(t, n, i + 1);
      return r;
    }
    if (strchr(t[i].s, '/')) {                                      // "1/28", "1/28/15", "1/28/2015"
      int a = 0, b = 0, c = 0;
      int k = sscanf(t[i].s, "%d/%d/%d", &a, &b, &c);
      if (k >= 2 && a >= 1 && a <= 12 && b >= 1 && b <= 31) {
        r.m = a; r.d = b;
        if (k == 3) r.y = c < 100 ? 2000 + c : c;
        return r;
      }
    }
  }
  return r;
}

// Completes a date without a year: the candidate year nearest publication, preferring a matching weekday.
bool chooseYear(DateHit* h, int wday, time_t published) {
  if (h->y) return validDate(h->y, h->m, h->d);
  if (!published) return false;
  int py = ymdOf(published).y;
  long long best = -1;
  int bestYear = 0;
  for (int y = py - 1; y <= py + 1; y++) {
    if (!validDate(y, h->m, h->d)) continue;
    long long dist = (long long)localTime(y, h->m, h->d, 12, 0) - (long long)published;
    if (dist < 0) dist = -dist;
    if (dist > (long long)YEAR_WINDOW) continue;
    if (wday >= 0 && weekdayAt(y, h->m, h->d) != wday) dist += 400LL * DAY;
    if (best < 0 || dist < best) { best = dist; bestYear = y; }
  }
  if (!bestYear) return false;
  h->y = bestYear;
  return true;
}

const char* const DECLARE_WORDS[] = { "declare", "declares", "declared", "declaring", "effective", "starting",
                                      "starts", "begins", "beginning", "issued", nullptr };
const char* const END_WORDS[] = { "lift", "lifts", "lifted", "lifting", "end", "ends", "ended", "ending",
                                  "cancel", "cancels", "canceled", "cancelled", "rescinded", "expire",
                                  "expires", "expired", nullptr };

SnowNoticeKind kindOf(const Tok* t, int n) {
  bool subject = false;
  for (int i = 0; i + 1 < n; i++) if (is(t[i], "snow") && is(t[i + 1], "emergency")) subject = true;
  if (!subject) return SNOW_NOTICE_NONE;
  for (int i = 0; i < n; i++) {
    if (is(t[i], "no") && i + 1 < n && is(t[i + 1], "longer")) return SNOW_NOTICE_ENDED;
    if (is(t[i], "called") && i + 1 < n && is(t[i + 1], "off")) return SNOW_NOTICE_ENDED;
    if (is(t[i], "in") && i + 1 < n && is(t[i + 1], "effect")) return SNOW_NOTICE_DECLARED;
    if (isIn(t[i], DECLARE_WORDS)) return SNOW_NOTICE_DECLARED;
    if (isIn(t[i], END_WORDS)) return SNOW_NOTICE_ENDED;
  }
  return SNOW_NOTICE_NONE;
}

// ---- Entities -------------------------------------------------------------------------------------

size_t putCodepoint(char* w, long cp) {
  if (cp == 0xA0) cp = ' ';
  else if (cp == 0x2018 || cp == 0x2019) cp = '\'';
  else if (cp == 0x201C || cp == 0x201D) cp = '"';
  else if (cp == 0x2013 || cp == 0x2014) cp = '-';
  if (cp <= 0) return 0;
  if (cp < 0x80) { w[0] = (char)cp; return 1; }
  if (cp < 0x800) { w[0] = (char)(0xC0 | (cp >> 6)); w[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
  if (cp < 0x10000) {
    w[0] = (char)(0xE0 | (cp >> 12)); w[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); w[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  if (cp > 0x10FFFF) return 0;
  w[0] = (char)(0xF0 | (cp >> 18)); w[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  w[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); w[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

// In place: the output is never longer than the entity it replaces.
void decodeEntities(char* s) {
  static const struct { const char* name; char c; } NAMED[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' }, { "nbsp", ' ' } };
  char* w = s;
  const char* r = s;
  while (*r) {
    if (*r == '&') {
      const char* semi = strchr(r, ';');
      if (semi && semi - r >= 3 && semi - r <= 9) {
        const char* e = r + 1;
        size_t len = (size_t)(semi - e);
        long cp = -1;
        if (e[0] == '#') {
          bool hex = e[1] == 'x' || e[1] == 'X';
          const char* digits = e + (hex ? 2 : 1);
          char* end;
          long v = strtol(digits, &end, hex ? 16 : 10);
          if (end == semi && end > digits) cp = v;
        } else {
          for (size_t k = 0; k < sizeof NAMED / sizeof NAMED[0]; k++)
            if (strlen(NAMED[k].name) == len && strncmp(e, NAMED[k].name, len) == 0) cp = (unsigned char)NAMED[k].c;
        }
        if (cp >= 0) { w += putCodepoint(w, cp); r = semi + 1; continue; }
      }
    }
    *w++ = *r++;
  }
  *w = 0;
}

void tidy(char* s) {                       // entities twice (Drupal sometimes double-encodes), whitespace once
  decodeEntities(s);
  decodeEntities(s);
  char* w = s;
  bool space = true;                       // drops leading whitespace
  for (const char* r = s; *r; r++) {
    char c = *r;
    if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (c == ' ' && space) continue;
    space = c == ' ';
    *w++ = c;
  }
  if (w > s && w[-1] == ' ') w--;
  *w = 0;
}

}  // namespace

// ---- Headlines --------------------------------------------------------------------------------------

SnowNotice parseSnowNotice(const char* title, time_t published) {
  SnowNotice r = { SNOW_NOTICE_NONE, false, 0 };
  if (!title) return r;
  Tok t[MAX_TOKENS];
  int n = tokenize(title, t, MAX_TOKENS, true);
  r.kind = kindOf(t, n);
  if (r.kind == SNOW_NOTICE_NONE) return r;

  Clock clock = findClock(t, n);
  if (!clock.found) return r;
  int wday = -1;
  bool tomorrow = false, today = false;
  for (int i = 0; i < n; i++) {
    if (wday < 0 && monthOf(t[i]) == 0) wday = weekdayOf(t[i]);     // ("mar" is March, not a weekday)
    if (is(t[i], "tomorrow")) tomorrow = true;
    if (is(t[i], "today") || is(t[i], "tonight")) today = true;
  }
  DateHit date = findDate(t, n);
  if (date.m) {
    if (!chooseYear(&date, wday, published)) return r;
  } else {
    if (!published) return r;
    Ymd p = ymdOf(published), d;
    if (wday >= 0) d = addDays(p, (wday - weekdayAt(p.y, p.m, p.d) + 7) % 7);
    else if (tomorrow) d = addDays(p, 1);
    else if (today) d = p;
    else return r;
    date.y = d.y; date.m = d.m; date.d = d.d;
  }
  r.when = clock.midnight ? localTime(date.y, date.m, date.d + 1, 0, 0)       // midnight ends the day
                          : localTime(date.y, date.m, date.d, clock.h, clock.m);
  r.timed = r.when != (time_t)-1;
  if (!r.timed) r.when = 0;
  return r;
}

time_t parseFeedDate(const char* s) {
  if (!s) return 0;
  Tok t[MAX_TOKENS];
  int n = tokenize(s, t, MAX_TOKENS, false);
  int y = 0, m = 0, d = 0;
  for (int i = 0; i < n; i++) {
    long v = plainNumber(t[i]);
    if (!y && strlen(t[i].s) == 4 && v >= 1970 && v < 2200) y = (int)v;
    int mo = monthOf(t[i]);
    if (mo && !m) {
      if (i + 1 < n && dayNumber(t[i + 1], &d)) m = mo;
      else if (i > 0 && dayNumber(t[i - 1], &d)) m = mo;
    }
  }
  if (!y || !m || !validDate(y, m, d)) return 0;
  return localTime(y, m, d, 12, 0);
}

const char* snowNoticeKindName(SnowNoticeKind k) {
  return k == SNOW_NOTICE_DECLARED ? "declared" : k == SNOW_NOTICE_ENDED ? "ended" : "none";
}

// ---- RSS ----------------------------------------------------------------------------------------------

void RssScanner::reset() {
  n_ = 0; dropped_ = 0; state_ = TEXT; tagLen_ = 0; tagLast_ = 0; inItem_ = false; sawFeed_ = false;
  cap_ = nullptr; capLen_ = 0; capMax_ = 0; pend_ = 0; bytes_ = 0;
  memset(&cur_, 0, sizeof cur_);
}

void RssScanner::put(char c) {
  if (cap_ && capLen_ + 1 < capMax_) { cap_[capLen_++] = c; cap_[capLen_] = 0; }
}

void RssScanner::capture(char* field, size_t size) {
  if (field[0]) return;                    // the first of pubDate / published / dc:date wins
  cap_ = field; capLen_ = 0; capMax_ = size;
}

void RssScanner::finishItem() {
  tidy(cur_.title); tidy(cur_.pubDate); tidy(cur_.link);
  if (n_ < MAX_ITEMS) items_[n_++] = cur_;
  else dropped_++;
}

void RssScanner::endTag() {
  tag_[tagLen_] = 0;
  const char* p = tag_;
  bool closing = *p == '/';
  if (closing) p++;
  if (*p == '?' || *p == '!') return;
  char name[16];
  size_t k = 0;
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '/' && k < sizeof name - 1) name[k++] = *p++;
  name[k] = 0;
  bool selfClosing = tagLast_ == '/';
  bool isItem = strcmp(name, "item") == 0 || strcmp(name, "entry") == 0;
  if (closing) {
    if (isItem && inItem_) { finishItem(); inItem_ = false; }
    cap_ = nullptr;
    return;
  }
  if (!strcmp(name, "rss") || !strcmp(name, "channel") || !strcmp(name, "feed") || !strcmp(name, "rdf:RDF")) sawFeed_ = true;
  if (isItem) { inItem_ = !selfClosing; memset(&cur_, 0, sizeof cur_); cap_ = nullptr; return; }
  if (!inItem_ || selfClosing) return;
  if (!strcmp(name, "title")) capture(cur_.title, sizeof cur_.title);
  else if (!strcmp(name, "pubDate") || !strcmp(name, "published") || !strcmp(name, "dc:date")) capture(cur_.pubDate, sizeof cur_.pubDate);
  else if (!strcmp(name, "link")) capture(cur_.link, sizeof cur_.link);
}

void RssScanner::push(char c) {
  bytes_++;
  switch (state_) {
    case TEXT:
      if (c == '<') { state_ = TAG; tagLen_ = 0; tagLast_ = 0; }
      else put(c);
      return;
    case TAG:
      if (c == '>') { endTag(); state_ = TEXT; return; }
      tagLast_ = c;                           // (the tag text itself may be truncated)
      if (tagLen_ < sizeof tag_ - 1) tag_[tagLen_++] = c;
      if (tagLen_ == 8 && memcmp(tag_, "![CDATA[", 8) == 0) { state_ = CDATA; pend_ = 0; }
      else if (tagLen_ == 3 && memcmp(tag_, "!--", 3) == 0) { state_ = COMMENT; pend_ = 0; }
      return;
    case CDATA:                               // raw text up to "]]>"
      if (c == ']') { pend_++; return; }
      if (c == '>' && pend_ >= 2) { for (int i = 2; i < pend_; i++) put(']'); pend_ = 0; state_ = TEXT; return; }
      for (int i = 0; i < pend_; i++) put(']');
      pend_ = 0;
      put(c);
      return;
    case COMMENT:                             // skipped up to "-->"
      if (c == '-') { pend_++; return; }
      if (c == '>' && pend_ >= 2) state_ = TEXT;
      pend_ = 0;
      return;
  }
}

// ---- Episodes -------------------------------------------------------------------------------------------

namespace {
time_t startOfDay(time_t t) { Ymd d = ymdOf(t); return localTime(d.y, d.m, d.d, 0, 0); }
time_t absDiff(time_t a, time_t b) { return a > b ? a - b : b - a; }
}  // namespace

void SnowEmergency::apply(const SnowNotice& n, time_t published, time_t now) {
  SnowEmergencyState& s = state;
  if (n.kind == SNOW_NOTICE_DECLARED) {
    if (!n.timed) return;                                   // "...remains in effect", "...declared Monday"
    time_t w = n.when;
    bool open = s.start != 0 && (s.end == 0 || w < s.end);
    if (open && absDiff(w, s.start) <= SAME_EPISODE) { s.start = w; return; }   // the same one, revised
    if (s.start == 0 || w > s.start) { s.start = w; s.end = 0; }               // a new one
    return;
  }
  if (n.kind != SNOW_NOTICE_ENDED) return;
  if (!n.timed) {                                           // "Snow Emergency Lifted": ends it now
    if (s.start != 0 && s.end == 0 && published != 0 && startOfDay(published) >= startOfDay(s.start))
      s.end = now > s.start ? now : s.start;
    return;
  }
  time_t e = n.when;
  bool ours = s.start != 0 && e > s.start &&
              (s.end == 0 ? e - s.start <= MAX_EPISODE : absDiff(e, s.end) <= SAME_EPISODE);
  if (ours) { s.end = e; return; }
  // An end still ahead with no emergency to attach it to: the declaration was missed.
  bool stale = s.start == 0 || e > (s.end ? s.end : s.start) + SAME_EPISODE;
  if (e > now && stale) {
    time_t begun = published ? startOfDay(published) : now;
    if (begun >= e) begun = now < e ? now : e - HOUR;
    s.start = begun;
    s.end = e;
  }
}

int SnowEmergency::update(const RssScanner& rss, time_t now) {
  int seen = 0;
  for (int i = rss.count() - 1; i >= 0; i--) {             // oldest first
    const RssItem& it = rss.item(i);
    time_t published = parseFeedDate(it.pubDate);
    SnowNotice n = parseSnowNotice(it.title, published);
    if (n.kind == SNOW_NOTICE_NONE) continue;
    seen++;
    apply(n, published, now);
  }
  return seen;
}

SnowEmergencyPhase SnowEmergency::phase(time_t now) const {
  if (state.start == 0) return SNOW_EMERGENCY_NONE;
  if (state.end != 0 && now >= state.end) return SNOW_EMERGENCY_NONE;
  if (state.end == 0 && now >= state.start + SNOW_EMERGENCY_MAX_OPEN_HOURS * HOUR) return SNOW_EMERGENCY_NONE;
  return now < state.start ? SNOW_EMERGENCY_DECLARED : SNOW_EMERGENCY_IN_EFFECT;
}

const char* snowEmergencyPhaseName(SnowEmergencyPhase p) {
  return p == SNOW_EMERGENCY_DECLARED ? "declared" : p == SNOW_EMERGENCY_IN_EFFECT ? "in effect" : "none";
}
