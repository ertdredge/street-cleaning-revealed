// Host tests for snow-risk.cpp.
#include <stdio.h>
#include <math.h>
#include "snow-risk.h"

static int failed = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { failed++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

static SnowOutlook day(bool valid, float inches, int pct, time_t start) {
  SnowOutlook s; s.valid = valid; s.amountMm = inches * 25.4f; s.probabilityPercent = pct; s.startTime = start; return s;
}

int main() {
  SnowOutlook none = day(true, 0, 0, 0), stale = day(false, 6, 100, 1000);
  SnowEvent ev[4];
  SnowOutlook w[4];

  w[0] = none; w[1] = none; w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 0);
  w[0] = stale; w[1] = stale; w[2] = stale; w[3] = stale;      // no fresh data: no snow
  CHECK(snowEventsFrom(w, 4, ev, 4) == 0);

  w[0] = none; w[1] = day(true, 6, 100, 5000); w[2] = none; w[3] = none;     // tomorrow only
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1);
  CHECK(ev[0].any && fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 100 && ev[0].start == 5000);
  w[0] = day(true, 3, 80, 2000); w[1] = day(true, 3, 100, 90000);           // storm across midnight: add up
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1);
  CHECK(fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 100 && ev[0].start == 2000);
  w[0] = stale; w[1] = day(true, 2, 50, 7000); w[2] = none; w[3] = none;
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && fabsf(ev[0].inches - 2) < 0.01f && ev[0].percent == 50 && ev[0].start == 7000);
  w[0] = none; w[1] = none; w[2] = none; w[3] = day(true, 5, 90, 300000);   // three days out: seen
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && ev[0].start == 300000 && fabsf(ev[0].inches - 5) < 0.01f);
  w[0] = day(true, 1, 40, 1000); w[1] = none; w[2] = none; w[3] = day(true, 5, 90, 300000);   // separate storms
  CHECK(snowEventsFrom(w, 4, ev, 4) == 2);
  CHECK(ev[0].start == 1000 && fabsf(ev[0].inches - 1) < 0.01f && ev[0].percent == 40);
  CHECK(ev[1].start == 300000 && fabsf(ev[1].inches - 5) < 0.01f && ev[1].percent == 90);
  CHECK(snowEventsFrom(w, 4, ev, 1) == 1 && ev[0].start == 1000);           // output limit respected
  w[0] = none; w[1] = day(true, 1, 40, 5), w[2] = day(true, 2, 70, 6); w[3] = day(true, 3, 60, 7);   // a three-day storm
  CHECK(snowEventsFrom(w, 4, ev, 4) == 1 && fabsf(ev[0].inches - 6) < 0.01f && ev[0].percent == 70 && ev[0].start == 5);

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
