// Snow removal parking risk from the NWS forecast (docs/Ambient-Display-Encoding.md, 1.2).
// Pure calculation, tested on the host (`make test` in test/). Only NWS forecast data goes in; actual
// snow emergencies declared by the city are not used yet.
//
// A forecast snow EVENT is built from the SnowOutlooks of today and the next three days, which is at
// least 72 hours ahead. A storm across midnight is split across days by the feed, so each run of
// consecutive snowy days is one event: total inches, the largest probability proxy, and the first start
// time. Runs separated by a snow-free day are separate events, and the riskiest one counts. The risk is
// the street cleaning risk function evaluated as if cleaning ended when the snow starts, scaled down
// for a small or uncertain storm.
#pragma once
#include <time.h>
#include "weather-forecast.h"

struct SnowEvent {
  bool   any;        // measurable snow is forecast
  float  inches;     // total forecast accumulation
  int    percent;    // 0..100 (the feed's proxy: chance of precipitation given snow)
  time_t start;      // first hour with measurable snow
};

// Groups `n` consecutive days (day 0 = today) into events, earliest first; at most `maxOut` events are
// written and the count is returned. Days without fresh data count as no snow.
int snowEventsFrom(const SnowOutlook* days, int n, SnowEvent* out, int maxOut);

// 1.2: `baseRisk` is the street cleaning risk function for the hours until the snow starts. At 100%
// and 4 inches or more it is used unchanged; below 4 inches it is multiplied by inches / 4, and
// below 100% by percent / 100. Result 0..90; 0 when there is no event.
int snowRiskFrom(const SnowEvent& e, int baseRisk);

// Which bar carries the snow risk. During a snow emergency parking is allowed on only one side of
// the street: the left side when winter begins in an even-numbered year (winter 2026/27), the right
// side when it begins in an odd-numbered year (2027/28), alternating from then on. So the snow risk
// belongs to the OTHER bar: the right bar in winter 2026/27, the left bar in 2027/28, and so on.
// A winter "begins" in SNOW_YEAR_START_MONTH (July: mid-year) of the calendar year it is named for.
const int SNOW_YEAR_START_MONTH = 7;
bool snowRiskOnRightBar(int year, int month);    // month 1..12

// The emergency-size storm (inches).
const float SNOW_EMERGENCY_INCHES = 4.0f;
