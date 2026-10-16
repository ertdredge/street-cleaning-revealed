// Waste collection calendar: holidays and the "put the trash out?" state.
// Pure calendar logic: no hardware, no FastLED, so it can be tested on a host.
//
// Regular pickup is Wednesday morning. A listed holiday falling on Sunday through
// Wednesday of a week delays that week's pickup to Thursday morning.
#pragma once
#include <time.h>

enum WasteState {
  WASTE_IDLE,      // nothing to do
  WASTE_PUT_OUT,   // put the trash out now
  WASTE_SKIPPED    // holiday delay: the regular Wednesday pickup is skipped; don't put it out yet
};

// State at `now` (local time; the TZ must already be set).
//   normal week:  Tue 15:00 -> Wed 12:00 PUT_OUT
//   delayed week: Tue 15:00 -> Wed 15:00 SKIPPED, Wed 15:00 -> Thu 12:00 PUT_OUT
// If `delayedBy` is given it is set to the causing holiday's name when the week is
// delayed and `now` is inside the Tuesday 15:00 -> Thursday 12:00 span, else nullptr.
WasteState wasteState(time_t now, const char** delayedBy = nullptr);

// Name of the holiday on this date, or nullptr. mon is 0..11 (struct tm convention).
// Holiday dates are computed arithmetically, so any year works and nothing is stored.
const char* holidayName(int year, int mon, int mday);
