// Waste collection calendar: holidays and the "put the trash out?" state.
// Pure calendar logic: no hardware, no FastLED, so it can be tested on a host.
//
// Regular pickup is Wednesday morning. A listed holiday falling on Sunday through
// Wednesday of a week delays that week's pickup to Thursday morning.
#pragma once
#include <time.h>

enum WasteState {
  WASTE_IDLE,           // nothing to do
  WASTE_PUT_OUT,        // put the trash out now
  WASTE_SKIPPED,        // holiday delay: the regular Wednesday pickup is skipped; don't put it out yet
  WASTE_PUT_OUT_YARD    // put the trash out now; it is also a yard waste collection
};

// State at `now` (local time; the TZ must already be set).
//   normal week:  Tue 15:00 -> Wed 12:00 PUT_OUT
//   delayed week: Tue 15:00 -> Wed 15:00 SKIPPED, Wed 15:00 -> Thu 12:00 PUT_OUT
// PUT_OUT becomes PUT_OUT_YARD in yard waste weeks (see yardWasteWeek).
// If `delayedBy` is given it is set to the causing holiday's name when the week is
// delayed and `now` is inside the Tuesday 15:00 -> Thursday 12:00 span, else nullptr.
WasteState wasteState(time_t now, const char** delayedBy = nullptr);

// Is the week (Sunday-Saturday) that starts on the Sunday year/mon/sundayMday a yard waste week?
// Weekly from the 2nd Sunday in April through the 2nd Saturday in May; then alternating weeks
// starting with a trash-only week; weekly again from the 2nd Sunday in October; none from the
// 2nd Sunday in December until the following April. A holiday delay moves the pickup within
// its week and does not change the week's type.
bool yardWasteWeek(int year, int mon, int sundayMday);

// Name of the holiday on this date, or nullptr. mon is 0..11 (struct tm convention).
// Holiday dates are computed arithmetically, so any year works and nothing is stored.
const char* holidayName(int year, int mon, int mday);
