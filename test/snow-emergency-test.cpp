// Host tests for snow-emergency.cpp: every snow emergency headline the city used in 2024-2026 (the
// email subjects and the news titles), older Somerville Times wordings, non-notices, the RSS scanner
// and the episode rules.
//
//   ./snow-emergency-test                       run the tests
//   ./snow-emergency-test FEED.xml [NOW]        show how a saved feed is read ("-" reads stdin), e.g.
//   curl -s https://www.somervillema.gov/news.xml | ./snow-emergency-test -
//   ./snow-emergency-test data/snow-emergency/news-2026-01-26-ending.xml "2026-01-26 15:00"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "snow-emergency.h"

static int failed = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static time_t local(int y, int mo, int d, int h, int mi) {
  struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}
static time_t day(int y, int mo, int d) { return local(y, mo, d, 12, 0); }   // what parseFeedDate returns

static void fmt(time_t t, char* buf, size_t n) {
  if (!t) { snprintf(buf, n, "-"); return; }
  struct tm lt;
  localtime_r(&t, &lt);
  strftime(buf, n, "%a %Y-%m-%d %H:%M", &lt);
}

// A headline that must parse to `kind` at `when` (when == 0: the kind, but untimed).
static void expectNotice(int line, const char* title, time_t published, SnowNoticeKind kind, time_t when) {
  SnowNotice n = parseSnowNotice(title, published);
  bool ok = n.kind == kind && n.timed == (when != 0) && n.when == when;
  checks++;
  if (!ok) {
    failed++;
    char got[40], want[40];
    fmt(n.when, got, sizeof got); fmt(when, want, sizeof want);
    printf("FAIL line %d: \"%s\"\n    got %s %s, want %s %s\n", line, title,
           snowNoticeKindName(n.kind), got, snowNoticeKindName(kind), want);
  }
}
#define NOTICE(title, pub, kind, when) expectNotice(__LINE__, title, pub, kind, when)

struct FileSource {                            // what scanRss needs: readBytes() returning 0 at the end
  FILE* f;
  explicit FileSource(const char* path) : f(strcmp(path, "-") == 0 ? stdin : fopen(path, "rb")) {}
  ~FileSource() { if (f && f != stdin) fclose(f); }
  size_t readBytes(char* buf, size_t n) { return f ? fread(buf, 1, n, f) : 0; }
};

static bool scanFile(const char* path, RssScanner& rss) {
  FileSource src(path);
  if (!src.f) return false;
  rss.reset();
  scanRss(src, rss);
  return true;
}

static int showFeed(const char* path, const char* nowText) {
  static RssScanner rss;
  if (!scanFile(path, rss)) { printf("cannot open %s\n", path); return 1; }
  time_t now = time(nullptr);
  int y, mo, d, h, mi;
  if (nowText && sscanf(nowText, "%d-%d-%d %d:%d", &y, &mo, &d, &h, &mi) == 5) now = local(y, mo, d, h, mi);
  printf("%u bytes, %s, %d items (%d dropped)\n", (unsigned)rss.bytes(), rss.sawFeed() ? "a feed" : "NOT a feed",
         rss.count(), rss.dropped());
  for (int i = 0; i < rss.count(); i++) {
    const RssItem& it = rss.item(i);
    time_t pub = parseFeedDate(it.pubDate);
    SnowNotice n = parseSnowNotice(it.title, pub);
    char when[40];
    fmt(n.when, when, sizeof when);
    printf("%2d  %-28s %-9s %-21s %s\n", i, it.pubDate, snowNoticeKindName(n.kind),
           n.kind == SNOW_NOTICE_NONE ? "" : (n.timed ? when : "(no time)"), it.title);
  }
  SnowEmergency e;
  int seen = e.update(rss, now);
  char a[40], b[40], c[40];
  fmt(now, a, sizeof a); fmt(e.state.start, b, sizeof b); fmt(e.state.end, c, sizeof c);
  printf("now %s: %d notices; start %s, end %s; phase %s\n", a, seen, b, c, snowEmergencyPhaseName(e.phase(now)));
  return 0;
}

int main(int argc, char** argv) {
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);                   // the rule the sketch uses
  tzset();
  if (argc > 1) return showFeed(argv[1], argc > 2 ? argv[2] : nullptr);

  const SnowNoticeKind D = SNOW_NOTICE_DECLARED, E = SNOW_NOTICE_ENDED, N = SNOW_NOTICE_NONE;

  // ---- Feed dates -------------------------------------------------------------------------------
  CHECK(parseFeedDate("Thursday, October 29, 2026") == day(2026, 10, 29));
  CHECK(parseFeedDate("  Saturday, January 24, 2026 ") == day(2026, 1, 24));
  CHECK(parseFeedDate("Thu, 29 Oct 2026 14:05:00 -0400") == day(2026, 10, 29));
  CHECK(parseFeedDate("2026-10-29") == 0);                     // ISO dates are not used by the feed
  CHECK(parseFeedDate("") == 0 && parseFeedDate(nullptr) == 0);
  CHECK(parseFeedDate("February 30, 2026") == 0);

  // ---- 2024-2026: email subject, then the news headline where one was posted ----------------------
  // January 2024 (no news items)
  NOTICE("Snow Emergency Declared for 4 p.m. on Saturday, January 6, 2024", day(2024, 1, 5), D, local(2024, 1, 6, 16, 0));
  NOTICE("Snow Emergency Will Be Lifted at 4 p.m, Sunday, January 7", day(2024, 1, 7), E, local(2024, 1, 7, 16, 0));
  // February 2024
  NOTICE("Snow Emergency in Effect Starting 7 p.m., Monday, Feb 12, 2024", day(2024, 2, 12), D, local(2024, 2, 12, 19, 0));
  NOTICE("Somerville Snow Emergency Lifted Tuesday, February 13, at 10 a.m.", day(2024, 2, 13), E, local(2024, 2, 13, 10, 0));
  NOTICE("SOMERVILLE SNOW EMERGENCY ENDS Tuesday, Feb. 13, at 10 a.m. MOVE CARS FROM CITY LOTS BY 12 NOON",
         day(2024, 2, 13), E, local(2024, 2, 13, 10, 0));
  // February 2025
  NOTICE("Snow Emergency Declared for Saturday, February 8, 2025, 5 p.m.", day(2025, 2, 8), D, local(2025, 2, 8, 17, 0));
  NOTICE("Snow Emergency Lifted at 12 p.m. noon, Sunday, February 9", day(2025, 2, 9), E, local(2025, 2, 9, 12, 0));
  NOTICE("Snow Emergency Ends Feb. 9 at Noon; Move Cars From City Lots by 2 p.m. Sunday.", day(2025, 2, 9), E, local(2025, 2, 9, 12, 0));
  // January 2026
  NOTICE("Snow Emergency Declared for 7 a.m. on Sunday, January 25, 2026", day(2026, 1, 24), D, local(2026, 1, 25, 7, 0));
  NOTICE("Somerville Declares Snow Emergency Effective Sunday, January 25, at 7 a.m.", day(2026, 1, 24), D, local(2026, 1, 25, 7, 0));
  NOTICE("Snow Emergency will end at 12 p.m. (noon), Tuesday, January 27", day(2026, 1, 26), E, local(2026, 1, 27, 12, 0));
  NOTICE("Somerville Snow Emergency Will End Tuesday, January 27, at 12 Noon", day(2026, 1, 26), E, local(2026, 1, 27, 12, 0));
  // February 2026
  NOTICE("Snow Emergency Declared for 12 p.m. (noon) on Sunday, February 22, 2026", day(2026, 2, 21), D, local(2026, 2, 22, 12, 0));
  NOTICE("Somerville Declares Snow Emergency Effective Sunday, February 22, at 12 p.m. Noon", day(2026, 2, 21), D, local(2026, 2, 22, 12, 0));
  NOTICE("Snow Emergency Will End at 8 p.m., Tuesday, February 24", day(2026, 2, 23), E, local(2026, 2, 24, 20, 0));
  NOTICE("Somerville Snow Emergency Will End Tuesday, February 24 at 8 p.m.", day(2026, 2, 23), E, local(2026, 2, 24, 20, 0));

  // ---- Older wordings (The Somerville Times reprints) ----------------------------------------------
  NOTICE("Somerville Snow Emergency to lift at 4 p.m. Monday, March 4", day(2019, 3, 3), E, local(2019, 3, 4, 16, 0));
  NOTICE("Somerville snow emergency lifted at 9:30 a.m., March 2", day(2018, 3, 2), E, local(2018, 3, 2, 9, 30));
  NOTICE("Snow emergency lifting at noon Wed. 1/28", day(2015, 1, 27), E, local(2015, 1, 28, 12, 0));
  NOTICE("Snow Emergency lifted as of 11 a.m. Sunday, Dec. 15", day(2013, 12, 15), E, local(2013, 12, 15, 11, 0));
  NOTICE("Somerville snow emergency lifted at noon, Jan. 20", day(2019, 1, 20), E, local(2019, 1, 20, 12, 0));
  NOTICE("Somerville\xE2\x80\x99s snow emergency lifted", day(2015, 3, 11), E, 0);
  NOTICE("Somerville declares snow emergency Monday", day(2015, 1, 25), D, 0);

  // ---- Other forms the grammar accepts ---------------------------------------------------------------
  NOTICE("Snow Emergency Declared for 7 p.m., Friday, January 1", day(2026, 12, 31), D, local(2027, 1, 1, 19, 0));   // across New Year
  NOTICE("Snow Emergency Ends at Midnight Sunday, March 1", day(2026, 2, 28), E, local(2026, 3, 2, 0, 0));   // end of Sunday (AP)
  NOTICE("Snow Emergency Declared Starting 6pm Tonight", day(2026, 12, 9), D, local(2026, 12, 9, 18, 0));
  NOTICE("Snow Emergency Begins Tomorrow at 7 a.m.", day(2026, 12, 9), D, local(2026, 12, 10, 7, 0));
  NOTICE("Snow Emergency Declared for Thursday at 9 p.m.", day(2026, 12, 8), D, local(2026, 12, 10, 21, 0));   // next Thursday
  NOTICE("Snow emergency declared for 1/28/15 at 7 a.m.", day(2015, 1, 27), D, local(2015, 1, 28, 7, 0));
  NOTICE("Snow Emergency in effect 13 February 2024 from 7 p.m.", 0, D, local(2024, 2, 13, 19, 0));   // full date: no pubDate needed
  NOTICE("Snow Emergency No Longer in Effect as of 3 p.m. Today", day(2026, 12, 11), E, local(2026, 12, 11, 15, 0));
  NOTICE("Snow Emergency Called Off; Parking Ban Will Not Start at 7 p.m.", day(2026, 12, 11), E, 0);   // after ';' ignored
  NOTICE("Snow Emergency Cancelled", day(2026, 12, 11), E, 0);
  NOTICE("Somerville Declares Snow Emergency Effective Sunday, January 25, at 7 a.m.", 0, D, 0);   // no year, no pubDate
  NOTICE("Snow Emergency Declared for 7 a.m. on Sunday, January 25", day(2025, 7, 25), D, local(2026, 1, 25, 7, 0));   // 2025 is nearer, but a Saturday

  // ---- Not notices --------------------------------------------------------------------------------------
  NOTICE("Snow Emergency Parking Exceptions for Residents with Disabilities", day(2026, 1, 22), N, 0);
  NOTICE("What Happens When a Snow Emergency Is Declared?", day(2026, 1, 20), D, 0);    // a declaration, but no time: ignored
  NOTICE("Teen Shoveling Program Seeks Volunteers", day(2026, 1, 20), N, 0);
  NOTICE("Hazardous and Yard Waste Pickup Deadlines Approaching", day(2026, 10, 29), N, 0);
  NOTICE("Community Invited to Domestic Violence Vigil on Wednesday, October 28", day(2026, 10, 21), N, 0);
  NOTICE("Winter Weekend Events Declared a Success at 4 p.m. Ceremony", day(2026, 2, 1), N, 0);   // no "snow emergency"
  NOTICE("Snow emergencies: what to know before 7 p.m. Friday", day(2026, 12, 1), N, 0);         // plural: general advice
  NOTICE("", day(2026, 12, 1), N, 0);
  CHECK(parseSnowNotice(nullptr, 0).kind == N);

  // ---- RSS scanner --------------------------------------------------------------------------------------
  static RssScanner rss;
  CHECK(scanFile(DATA_DIR "/snow-emergency/news-2026-10-29.xml", rss));
  CHECK(rss.sawFeed() && rss.count() == 10 && rss.dropped() == 0);
  CHECK(strcmp(rss.item(0).title, "Join the Somerville Human Rights Commission") == 0);
  CHECK(strcmp(rss.item(0).pubDate, "Thursday, October 29, 2026") == 0);
  CHECK(strcmp(rss.item(0).link, "https://www.somervillema.gov/news/join-somerville-human-rights-commission-0") == 0);
  CHECK(strcmp(rss.item(8).title, "Lydia Warren Named Somerville Arts & Culture Director") == 0);
  {
    SnowEmergency e;
    CHECK(e.update(rss, local(2026, 10, 30, 11, 0)) == 0 && e.state.start == 0);
  }

  CHECK(scanFile(DATA_DIR "/snow-emergency/encoded.xml", rss));
  CHECK(rss.count() == RssScanner::MAX_ITEMS && rss.dropped() == 3);
  CHECK(strcmp(rss.item(0).title, "Somerville Snow Emergency Will End Tuesday, February 24 at 8 p.m.") == 0);
  CHECK(strcmp(rss.item(1).title, "Somerville Declares Snow Emergency Effective Sunday, February 22, at 12 p.m. Noon") == 0);
  CHECK(strcmp(rss.item(2).title, "Somerville's Teen Shoveling Program 's Busy Week - Thanks!") == 0);   // the commented-out and empty items are skipped
  CHECK(strcmp(rss.item(3).title, "Odd ]] title ]]] here") == 0);
  CHECK(strcmp(rss.item(15).title, "Filler item 12") == 0);
  {
    SnowEmergency e;
    CHECK(e.update(rss, local(2026, 2, 21, 18, 0)) == 2);
    CHECK(e.state.start == local(2026, 2, 22, 12, 0) && e.state.end == local(2026, 2, 24, 20, 0));
  }

  {                                                              // byte by byte gives the same as in bulk
    static RssScanner one;
    FILE* f = fopen(DATA_DIR "/snow-emergency/encoded.xml", "rb");
    CHECK(f != nullptr);
    int c;
    while (f && (c = fgetc(f)) != EOF) one.push((char)c);
    if (f) fclose(f);
    CHECK(one.count() == rss.count());
    for (int i = 0; i < one.count() && i < rss.count(); i++)
      CHECK(strcmp(one.item(i).title, rss.item(i).title) == 0 && strcmp(one.item(i).pubDate, rss.item(i).pubDate) == 0);
  }

  CHECK(scanFile(DATA_DIR "/synthetic/error-page.html", rss));   // an HTML error page is not a feed
  CHECK(!rss.sawFeed() && rss.count() == 0);
  CHECK(scanFile(DATA_DIR "/synthetic/empty.json", rss));
  CHECK(!rss.sawFeed() && rss.count() == 0);
  rss.reset();
  rss.push("<rss><channel><item><title>Snow Emergency Declared for 7 a.m. on Sunday, January 25, 2026</title><pubDa", 101);
  CHECK(rss.sawFeed() && rss.count() == 0);                     // a truncated item is not used

  // ---- January 2026, as the feed told it -----------------------------------------------------------------
  {
    SnowEmergency e;
    CHECK(e.phase(local(2026, 1, 24, 18, 0)) == SNOW_EMERGENCY_NONE);
    CHECK(scanFile(DATA_DIR "/snow-emergency/news-2026-01-24-declared.xml", rss));
    CHECK(e.update(rss, local(2026, 1, 24, 18, 0)) == 2);        // the declaration, and "What Happens When..." (untimed)
    CHECK(e.state.start == local(2026, 1, 25, 7, 0) && e.state.end == 0);
    CHECK(e.phase(local(2026, 1, 24, 18, 0)) == SNOW_EMERGENCY_DECLARED);
    CHECK(e.phase(local(2026, 1, 25, 7, 0)) == SNOW_EMERGENCY_IN_EFFECT);
    CHECK(e.phase(local(2026, 1, 28, 6, 59)) == SNOW_EMERGENCY_IN_EFFECT);   // no end announced: 96 h
    CHECK(e.phase(local(2026, 1, 29, 7, 0)) == SNOW_EMERGENCY_NONE);
    CHECK(scanFile(DATA_DIR "/snow-emergency/news-2026-01-26-ending.xml", rss));
    e.update(rss, local(2026, 1, 26, 15, 0));
    CHECK(e.state.start == local(2026, 1, 25, 7, 0) && e.state.end == local(2026, 1, 27, 12, 0));
    CHECK(e.phase(local(2026, 1, 27, 11, 59)) == SNOW_EMERGENCY_IN_EFFECT);
    CHECK(e.phase(local(2026, 1, 27, 12, 0)) == SNOW_EMERGENCY_NONE);
    SnowEmergencyState before = e.state;                         // replaying the same feed changes nothing
    e.update(rss, local(2026, 1, 27, 13, 0));
    e.update(rss, local(2026, 1, 30, 13, 0));
    CHECK(e.state.start == before.start && e.state.end == before.end);
    SnowEmergency fresh;                                         // a device that boots after the end was posted
    fresh.update(rss, local(2026, 1, 26, 15, 0));
    CHECK(fresh.state.start == before.start && fresh.state.end == before.end);
  }

  // ---- Episode rules -----------------------------------------------------------------------------------
  const SnowNotice decl25 = parseSnowNotice("Somerville Declares Snow Emergency Effective Sunday, January 25, at 7 a.m.", day(2026, 1, 24));
  const SnowNotice end27 = parseSnowNotice("Somerville Snow Emergency Will End Tuesday, January 27, at 12 Noon", day(2026, 1, 26));
  const SnowNotice decl22 = parseSnowNotice("Somerville Declares Snow Emergency Effective Sunday, February 22, at 12 p.m. Noon", day(2026, 2, 21));
  const SnowNotice end24 = parseSnowNotice("Somerville Snow Emergency Will End Tuesday, February 24 at 8 p.m.", day(2026, 2, 23));
  {                                                              // a revised start replaces the first one
    SnowEmergency e;
    e.apply(decl25, day(2026, 1, 24), local(2026, 1, 24, 18, 0));
    e.apply(parseSnowNotice("Snow Emergency Now Starts at 5 a.m. Sunday, January 25", day(2026, 1, 24)), day(2026, 1, 24), local(2026, 1, 24, 20, 0));
    CHECK(e.state.start == local(2026, 1, 25, 5, 0) && e.state.end == 0);
  }
  {                                                              // a new emergency after an ended one; old items ignored
    SnowEmergency e;
    e.apply(decl25, day(2026, 1, 24), 0); e.apply(end27, day(2026, 1, 26), 0);
    e.apply(decl22, day(2026, 2, 21), local(2026, 2, 21, 18, 0));
    CHECK(e.state.start == local(2026, 2, 22, 12, 0) && e.state.end == 0);
    e.apply(decl25, day(2026, 1, 24), local(2026, 2, 21, 19, 0));
    e.apply(end27, day(2026, 1, 26), local(2026, 2, 21, 19, 0));
    CHECK(e.state.start == local(2026, 2, 22, 12, 0) && e.state.end == 0);
    e.apply(end24, day(2026, 2, 23), local(2026, 2, 23, 9, 0));
    CHECK(e.state.end == local(2026, 2, 24, 20, 0));
    e.apply(parseSnowNotice("Snow Emergency Will Now End at 6 p.m., Tuesday, February 24", day(2026, 2, 24)), day(2026, 2, 24), local(2026, 2, 24, 9, 0));
    CHECK(e.state.end == local(2026, 2, 24, 18, 0));             // a revised end replaces the first one
  }
  {                                                              // February 2024: the declaration was never posted
    SnowEmergency e;
    SnowNotice end13 = parseSnowNotice("Somerville Snow Emergency Lifted Tuesday, February 13, at 10 a.m.", day(2024, 2, 13));
    e.apply(end13, day(2024, 2, 13), local(2024, 2, 13, 7, 0));
    CHECK(e.state.start == local(2024, 2, 13, 0, 0) && e.state.end == local(2024, 2, 13, 10, 0));
    CHECK(e.phase(local(2024, 2, 13, 7, 0)) == SNOW_EMERGENCY_IN_EFFECT);
    CHECK(e.phase(local(2024, 2, 13, 10, 0)) == SNOW_EMERGENCY_NONE);
    SnowEmergency late;                                          // first seen after it ended: nothing to show
    late.apply(end13, day(2024, 2, 13), local(2024, 2, 13, 11, 0));
    CHECK(late.state.start == 0 && late.phase(local(2024, 2, 13, 11, 0)) == SNOW_EMERGENCY_NONE);
    SnowEmergency stuck;                                         // the previous one never got an end item either
    stuck.apply(parseSnowNotice("Snow Emergency Declared for 4 p.m. on Saturday, January 6, 2024", day(2024, 1, 5)), day(2024, 1, 5), local(2024, 1, 5, 20, 0));
    CHECK(stuck.phase(local(2024, 1, 10, 16, 0)) == SNOW_EMERGENCY_NONE);   // dropped after 96 h
    stuck.apply(end13, day(2024, 2, 13), local(2024, 2, 13, 7, 0));
    CHECK(stuck.state.start == local(2024, 2, 13, 0, 0) && stuck.state.end == local(2024, 2, 13, 10, 0));
  }
  {                                                              // an end with no time ends it when seen
    SnowEmergency e;
    e.apply(decl25, day(2026, 1, 24), 0);
    SnowNotice lifted = parseSnowNotice("Somerville's Snow Emergency Lifted", day(2026, 1, 27));
    e.apply(lifted, day(2026, 1, 23), local(2026, 1, 27, 9, 0));   // published before the start day: not this one
    CHECK(e.state.end == 0);
    e.apply(lifted, day(2026, 1, 27), local(2026, 1, 27, 9, 0));
    CHECK(e.state.end == local(2026, 1, 27, 9, 0));
    e.apply(lifted, day(2026, 1, 27), local(2026, 1, 27, 10, 0));  // seen again: unchanged
    CHECK(e.state.end == local(2026, 1, 27, 9, 0));
    CHECK(e.phase(local(2026, 1, 27, 9, 0)) == SNOW_EMERGENCY_NONE);
  }
  {                                                              // untimed declarations change nothing
    SnowEmergency e;
    e.apply(parseSnowNotice("What Happens When a Snow Emergency Is Declared?", day(2026, 1, 20)), day(2026, 1, 20), local(2026, 1, 20, 9, 0));
    CHECK(e.state.start == 0);
  }

  printf("snow-emergency-test: %d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
