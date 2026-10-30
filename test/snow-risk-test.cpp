// Host tests for snow-risk.cpp.
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include "snow-risk.h"

static int failed = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static time_t local(int y, int mo, int d, int h, int mi) {
  struct tm t = {}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d; t.tm_hour = h; t.tm_min = mi; t.tm_isdst = -1;
  return mktime(&t);
}

static SnowOutlook day(bool valid, float inches, int pct, time_t start) {
  SnowOutlook s; s.valid = valid; s.amountMm = inches * 25.4f; s.probabilityPercent = pct; s.startTime = start; return s;
}

int main() {
  setenv("TZ", "EST5EDT,M3.2.0,M11.1.0", 1);                   // the rule the sketch uses
  tzset();
  SnowOutlook none = day(true, 0, 0, 0), stale = day(false, 6, 100, 1000);
  SnowEvent ev[4];
  SnowOutlook w[4];
  time_t m29 = local(2026, 10, 29, 0, 0), m30 = local(2026, 10, 30, 0, 0), m31 = local(2026, 10, 31, 0, 0);

  w[0] = none; w[1] = none; w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 0);
  w[0] = stale; w[1] = stale; w[2] = stale; w[3] = stale;      // no fresh data: no snow
  CHECK(snowEventsFrom(w, 4, ev, 4) == 0);

  time_t t30_8 = local(2026, 10, 30, 8, 0);
  w[0] = none; w[1] = day(true, 6, 100, t30_8); w[2] = none; w[3] = none;     // tomorrow only
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1);
  CHECK(ev[0].any && fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 100 && ev[0].start == t30_8);
  w[0] = stale; w[1] = day(true, 2, 50, t30_8); w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && fabsf(ev[0].inches - 2) < 0.01f && ev[0].percent == 50 && ev[0].start == t30_8);
  time_t t1_5 = local(2026, 11, 1, 5, 0);
  w[0] = none; w[1] = none; w[2] = none; w[3] = day(true, 5, 90, t1_5);     // three days out: seen
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && ev[0].start == t1_5 && fabsf(ev[0].inches - 5) < 0.01f);

  // An earlier small snowfall must not hide a later bigger one: the real feed from 2026-10-28 has
  // 0.3 in at 22% starting at midnight today, and 0.4 in at 48% starting 08:00 tomorrow.
  w[0] = day(true, 0.3f, 22, m29); w[1] = day(true, 0.4f, 48, t30_8); w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 2);
  CHECK(ev[0].start == m29 && fabsf(ev[0].inches - 0.3f) < 0.01f && ev[0].percent == 22);
  CHECK(ev[1].start == t30_8 && fabsf(ev[1].inches - 0.4f) < 0.01f && ev[1].percent == 48);
  w[0] = day(true, 1, 40, local(2026, 10, 29, 6, 0)); w[1] = none; w[2] = none; w[3] = day(true, 5, 90, t1_5);   // separate storms
  CHECK(snowEventsFrom(w, 4, ev, 4) == 2 && ev[1].start == t1_5 && fabsf(ev[1].inches - 5) < 0.01f && ev[1].percent == 90);
  CHECK(snowEventsFrom(w, 4, ev, 1) == 1 && fabsf(ev[0].inches - 1) < 0.01f);   // output limit respected

  // A storm across midnight is split across days by the feed; a day whose snow starts at local midnight
  // continues the previous day's storm.
  w[0] = day(true, 3, 80, local(2026, 10, 29, 20, 0)); w[1] = day(true, 3, 100, m30); w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1);
  CHECK(fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 100 && ev[0].start == local(2026, 10, 29, 20, 0));
  w[0] = none; w[1] = day(true, 1, 40, local(2026, 10, 30, 22, 0)); w[2] = day(true, 2, 70, m31); w[3] = day(true, 3, 60, local(2026, 11, 1, 0, 0));   // a three-day storm
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 70 && ev[0].start == local(2026, 10, 30, 22, 0));
  w[0] = day(true, 1, 40, m29); w[1] = day(true, 2, 70, local(2026, 10, 30, 3, 0)); w[2] = none; w[3] = none;   // second day starts after midnight: separate
  CHECK(snowEventsFrom(w, 4, ev, 4) == 2);

  // 1.2: 100% and >= 4 in -> the base risk unchanged
  SnowEvent big = { true, 6.0f, 100, 1 };
  CHECK(snowRiskFrom(big, 90) == 90);
  CHECK(snowRiskFrom(big, 37) == 37);
  SnowEvent exact = { true, 4.0f, 100, 1 };
  CHECK(snowRiskFrom(exact, 60) == 60);
  // below 4 in: x inches/4
  SnowEvent two = { true, 2.0f, 100, 1 };
  CHECK(snowRiskFrom(two, 80) == 40);
  // below 100%: x percent/100
  SnowEvent half = { true, 6.0f, 50, 1 };
  CHECK(snowRiskFrom(half, 80) == 40);
  SnowEvent both = { true, 2.0f, 50, 1 };                   // both reductions
  CHECK(snowRiskFrom(both, 80) == 20);
  SnowEvent zero = { true, 0.0f, 100, 1 };
  CHECK(snowRiskFrom(zero, 80) == 0);
  CHECK(snowRiskFrom(big, 0) == 0);
  CHECK(snowRiskFrom(big, 200) == 90);                      // never above 90
  SnowEvent nan = { true, NAN, 100, 1 };
  CHECK(snowRiskFrom(nan, 50) == 50);                       // NaN inches reads as no reduction, not a crash

  // Which bar: winter 2026/27 (July 2026 .. June 2027) loads the right bar, then alternating
  CHECK(snowRiskOnRightBar(2026, 7) && snowRiskOnRightBar(2026, 12) && snowRiskOnRightBar(2027, 1) && snowRiskOnRightBar(2027, 6));
  CHECK(!snowRiskOnRightBar(2027, 7) && !snowRiskOnRightBar(2027, 12) && !snowRiskOnRightBar(2028, 1) && !snowRiskOnRightBar(2028, 6));
  CHECK(snowRiskOnRightBar(2028, 7) && snowRiskOnRightBar(2029, 3));
  CHECK(!snowRiskOnRightBar(2026, 6) && !snowRiskOnRightBar(2026, 1));      // winter 2025/26 was the other way

  printf("%d checks, %d failed\n", checks, failed);
  return failed ? 1 : 0;
}
