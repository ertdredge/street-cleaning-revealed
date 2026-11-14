# Street Cleaning ReveaLED — Ambient Display Encoding

---

## 1. Left and right bar graphs

### 1.1 Street cleaning

Street cleaning is represented as a "risk" level on the right and left 90 pixel displays.  The left display represents the left side of the street and the right side represents the right side of the street.

$remaining$ hours from the current time until the *end* of street cleaning on the appropriate side of the street is calculated, and a $risk$ of parking on that side of the street on a scale of 0-90 is calculated as this function of the number of hours remaining to the end of street cleaning:

$$
\text{risk} = \min\!\left(90,\ \max\!\left(1,\ \left\lfloor 1 + \frac{504 - remaining}{24} + 69\left(\frac{504 - remaining}{485}\right)^{6} \right\rfloor\right)\right)
$$

- The linear term $\frac{504 - remaining}{24}$ contributes 1 pixel per day.
- The sextic term $69\left(\frac{504 - remaining}{485}\right)^6$ supplies the steep final approach.
- The constant 485 puts the 90-pixel maximum at $remaining = 19$ h (5 pm the day before).
- The $\max(1,\cdot)$ clamp handles $remaining > 504$, and the $\min(90,\cdot)$ clamp handles $remaining < 19$.


Excel:
```
  RISK=MAX(1, MIN(INT(1 + (504-r)/24 + 69*((504-r)/485)^6), 90))
```

This makes the risk grow faster the closer it gets to 5pm the day before street cleaning, at which point it is at maximum. Color for the pixels on that side is derived from the risk: Fully green at 0 risk, fully yellow at 45 risk, fully red at 90 risk, and changing linerally in between.

### 1.2 Snow removal

Snow emergencies are declared if more than 4 inches of snow are predicted.  Parking $risk$ is a function of:

- $percent$ forecast of snow probability
- $inches$ of snow forecasted
- $hours$ until start of snow

If $percent$ is 100% and $inches$ forecast is >=4 we determine risk with the street cleaning function above, and the number of hours until the snow is forecast to start is used for $remaining$. So it acts as if there was scheduled street cleaning *ending* at the time the snow is forecasted by NWS to *begin*.

If the city declares an actual snow emergency then it is treated as a 100% chance of 4 inches of snow forecasted to start at the start time of the snow emergency.  Once the snow emergency start time arrives, parking $risk$ for the appropriate side of the street will remain at maximum until the end time of the snow emergency arrives.  After the end time of the snow emergency that snow emergency is disregarded in $risk$ calculations.

If inches forecast is < 4 then the risk function result is multiplied by $\frac{inches}{4}$. If $percent$ is less than 100% then the risk function result is multipled by $percent$.

If the risk function output is high enough to result in marching animation of the bottom LED strip due to snow forecast, a sparkle animation is added to the marching animation: Throughout the bottom LED strip pixels, with random pixels briefly flashing white.

#### 1.2.1 Details

- The forecast covers today and the next three days, so at least 72 hours ahead at any time of day (about 96 hours at the start of a day). Each snowy day is its own snow event with its own amount, probability and start time, and the risk is worked out for each event: the highest one is used, so an earlier small or unlikely snowfall never hides a later bigger one. The one exception is a storm that crosses midnight, which the feed splits across two days: a day whose snow starts at local midnight (the snow was already falling) continues the previous day's storm, and the two are added together (inches summed, largest probability, earliest start). Trace snow is ignored.
- "Multiplied by $percent$" means by $percent / 100$. The two reductions combine: 2 inches at 50% gives $risk \times \frac{2}{4} \times 0.5$.
- During a snow emergency parking is allowed on only one side of the street: the left side when winter begins in an even-numbered year (winter 2026/27), the right side when it begins in an odd-numbered year (2027/28), alternating every year. The snow risk therefore applies to the *other* bar only, the bar for the side where parking is banned: the right bar from mid-2026 to mid-2027, the left bar from mid-2027 to mid-2028, and so on. That bar shows the larger of its street cleaning risk and the snow risk; the other bar is unaffected by snow. A winter begins on July 1 (`SNOW_YEAR_START_MONTH`).
- The parking risk does not depend on which day the top strip is showing: it uses the hours until the forecast snow starts, whichever of the four forecast days that falls on, at any time of day.
- A sparkle (random pixels briefly flashing white, as in 3.3.1) appears in two places. On the bottom strip it appears when the snow risk is above 55, the marching threshold, whether or not the pattern is moving. Throughout the whole 90 pixels of the bar that carries the snow risk (lit or not) it appears whenever the snow risk is greater than that side's street cleaning risk, i.e. whenever snow is what is setting the bar's value. The other bar does not sparkle. The pattern marches away from the riskier side, so a snow risk above the other bar's risk makes it march away from the snow bar.
- The city's snow emergency comes from the city's news RSS feed (`snow-emergency-feed.md`), read hourly. While an emergency is declared or in effect it is one more snow event, at full size (100%, 4 inches) starting when the parking ban starts: the risk follows the street cleaning curve up to the ban and stays at 90 from the ban's start until its end. After the end it is ignored. The highest risk of all events, forecast or declared, counts, and it goes to the same bar as forecast snow risk.
- Once an emergency has ended, forecast snow events that started before its end are the same storm and are dropped, so they cannot hold the bar at 90 until local midnight. A forecast snowfall that starts after the end still counts.
- The declared emergency is saved to flash so that a reboot during an emergency does not lose it. An emergency whose end is never announced is dropped 96 hours after its start.
- While the clock is simulated, the city feed is left out of the bars like the forecast. The `emergency start|stop WHEN` serial command forces an emergency (WHEN is `now`, +H or -H hours, or a date and time) and always applies; `emergency off` returns to the feed.
- Snow that has already started has $remaining = 0$, so the risk is 90. The forecast cannot tell when a storm ends within a day, so the risk persists until that day's total drops out of the forecast at local midnight.
- While the clock is simulated (`time`, sweeps, `test-suite`) the real forecast is left out of the bars. The `snow` serial command always applies.

---

## 2. Bottom

The bottom emphasizes the information on the left and right static bar graphs and layer in information about trash and yard waste collection. Both strips always show the same color and animation.

### 2.1 Animation

- Parking $risk$ > 55 on either side: animated pixels marching *away* from the highest risk side
  - Speed determined by the higher risk, 55 = slowest, 90 = fastest
- Otherwise (including equal risk on both sides) the pattern is frozen

### 2.2 Color

| Color | Meaning | RGB |
|---|---|---|
| White | Standard: nothing to put out | 54, 54, 54 |
| Azure | Put out the trash (trash only) | 0, 50, 252 |
| Azure and spring green | Put out the trash and yard waste (azure ants on a spring green background) | 0, 50, 252 and 0, 72, 36 |
| Pink | Holiday delay: the regular Wednesday pickup is skipped, don't put it out yet | 90, 35, 60 |

Red, green and yellow mean "don't" and "safe" and are reserved for parking on the left and right bar graphs, so the bottom never reuses them. The colors here avoid every hue the bar graphs use (green at 118° through amber to red at 0°). All four are luminance-matched to the bar graph colors so no state looks brighter than another. Azure is pure blue plus a little green: pure blue can't reach the same luminance.

The bottom shows a pattern of "ants" (2 lit pixels out of every 6) on a background. For white, azure and pink the background is the same color dimmed. For yard waste the ants are azure, the same color as a trash-only week, on a spring green background, because trash and yard waste both go out. The earlier pairing of two close greens that differed only slightly in hue was too hard to see marching. The new pair is also luminance-matched, so the ants differ from the background by hue alone (228° against 150°).

### 2.3 Trash collection

Trash is collected on Wednesday mornings.

- **Normal week:** from Tuesday 3pm until Wednesday noon the color is azure (azure ants on spring green in a yard waste week, see 2.4); otherwise white.
- **Delayed week:** if any holiday in the table below falls on the Sunday, Monday, Tuesday or Wednesday of the week, pickup moves to Thursday morning:
  - Tuesday 3pm until Wednesday 3pm: **pink**, the regular pickup is skipped
  - Wednesday 3pm until Thursday noon: azure (azure ants on spring green in a yard waste week), time to put it out
  - otherwise white

Weeks run Sunday through Saturday. Holidays are matched by their actual calendar date; "observed" days are not applied, so a holiday on a Friday or Saturday does not delay that week's pickup, and a holiday on a Sunday delays the following Wednesday.

| Holiday | Date |
|---|---|
| New Year's Day | January 1 |
| Martin Luther King Jr. Day | 3rd Monday in January |
| Presidents' Day | 3rd Monday in February |
| Patriots' Day | 3rd Monday in April |
| Memorial Day | last Monday in May |
| Juneteenth | June 19 |
| Independence Day | July 4 |
| Labor Day | 1st Monday in September |
| Indigenous Peoples' Day | 2nd Monday in October |
| Veterans Day | November 11 |
| Christmas Day | December 25 |

NOTE: Thanksgiving Thursday and Friday also delay trash collection in the city, but those days are not relevant for this street that has Wednesday normal pickups.

### 2.4 Yard waste

Yard waste is collected together with trash on the same pickup day, so a yard waste week shows azure ants on a spring green background where a trash-only week shows azure ants on a dimmed azure background. Which weeks are yard waste weeks:

- **2nd Sunday in April through the 2nd Saturday in May:** every week
- **After the 2nd Saturday in May until the 2nd Sunday in October:** every other week, starting with a trash-only week (azure), then a yard waste week (azure ants on spring green), and so on, except that no yard waste is collected during the week of Independence Day (the Sunday-to-Saturday week containing July 4). That week is trash only, and the following week resumes the alternation where it left off, so the skipped week does not count
- **2nd Sunday in October until the 2nd Sunday in December:** every week
- **2nd Sunday in December until the 2nd Sunday in April:** none; trash only (azure)

The alternating weeks count from the 2nd Saturday in May, so which weeks have yard waste shifts from year to year. A holiday delay moves the pickup within its week; it does not change the week's type or the alternation.

---

## 3. Top

The top LED strip shows the weather forecast: the forecast high temperature, the probability of precipitation and any snow for the displayed day (today's until 5 pm, tomorrow's from 5 pm), and tonight's forecast low temperature (which night is shown rolls over separately, at 6 am; see 3.4.2).

The 60 pixels form a single scale, read like a number line or a thermometer laid on its side. The center of the strip (the boundary between pixels 29 and 30, counting from 0 at the left end) stands for "typical for the date". Warmer than typical is to the right and colder to the left, at **two pixels per degree Celsius**, so each half of the strip (30 pixels) spans 15ºC. Two pixels per ºC is about one pixel per ºF, which is an easy way to read it.

The high is a lit bar that grows from the center; the night's low is a dark notch. Mnemonic: *the day is the bright bar, the night is the dark gap; warmer is to the right.* Both are read from position and length, not only from color, so the display works for color-blind viewers.

Calculations are in degrees Celsius.

### 3.1 Background

All 60 pixels are lit, except where the night notch (3.2.2) is, in a pale green-tinted off-white, `WEATHER_TYPICAL` (35, 43, 38): hue about 143º, saturation about 19%. Its luminance (0.2126 R + 0.7152 G + 0.0722 B on the raw values, about 41) is 75% of the luminance of the scarlet and sky blue bar colors (54), so a bar differs from the background in brightness as well as in hue.

On a day whose forecast high is within half a degree of typical the whole strip is this background, apart from the notch.

The background replaces the earlier spring green (0, 72, 36). Spring green and scarlet had equal luminance and differed by hue alone, so for red–green color-blind viewers a scarlet bar on a spring green background nearly disappeared. A near-neutral background with a brightness difference keeps the bar visible to every viewer. The exact tint is to be confirmed on the installed strip, whose white point decides whether (35, 43, 38) reads as faintly green.

### 3.2 Temperatures

#### 3.2.1 Forecast high: the bar

The forecast high temperature for the displayed day is compared to the [typical](typical-high-temperatures.csv) high temperature for that date. The difference, $\Delta_{high}$ = forecast high − typical high, sets a bar of $n = \lfloor 2\,|\Delta_{high}| \rfloor$ pixels (one pixel per half degree, rounded down), at most 30.

- **Warmer than typical** ($\Delta_{high} \ge 0.5$): a scarlet (173, 24, 0) bar grows rightward from the center, on pixels 30 to $30 + n - 1$.
- **Colder than typical** ($\Delta_{high} \le -0.5$): a sky blue (38, 58, 66) bar grows leftward from the center, on pixels 29 down to $29 - n + 1$.
- **Within half a degree of typical:** no bar.

The bar is solid: every bar pixel is the full bar color, with no blending into the background. 15ºC or more from typical fills that half of the strip.

e.g. if the displayed day is April 23 and the forecast high is 22.8ºC, that is compared to the typical high for April 23 in Boston, 15.6ºC: the forecast is 7.2ºC above typical, so $n = \lfloor 14.4 \rfloor = 14$ and pixels 30 to 43 are scarlet.

#### 3.2.2 Tonight's low: the night notch

The forecast low temperature for the night shown (3.4.2) is compared to the [typical](typical-low-temperatures.csv) low temperature. The difference, $\Delta_{low}$ = forecast low − typical low, places a **notch** of two adjacent dark (unlit) pixels on the same scale as the bar.

The notch straddles the scale boundary

$$
b = 30 + \operatorname{sign}(\Delta_{low}) \cdot \lfloor 2\,|\Delta_{low}| \rfloor
$$

clamped to $1 \le b \le 59$: pixels $b - 1$ and $b$ are dark. A low within half a degree of typical puts the notch on pixels 29 and 30, at the center. A low 15ºC or more from typical puts it on the two end pixels of that side (0 and 1, or 58 and 59).

The boundary $b$ is where the tip of a bar for the same deviation would be, so the notch and the bar share one scale: a notch right of center is a warmer than typical night, left of center a colder one.

e.g. a low 2.3ºC below typical gives $b = 30 - \lfloor 4.6 \rfloor = 26$: pixels 25 and 26 are dark.

The notch is drawn over the background and over the bar alike. Where it falls inside the bar it shows as a two-pixel gap in the bar. When the low and the high are about equally far from typical in the same direction (common, since one air mass usually sets both) the notch sits at the bar's tip: the bar's last pixel and the first background pixel beyond it are dark.

#### 3.2.3 Details

- The high is compared using the displayed day (today's, or tomorrow's from 5 pm; 3.4.2).
- The low of a night is compared with the typical low for the date of the **morning on which the night ends**: the minimum usually comes near dawn, and a daily normal minimum belongs to the calendar day it occurs in. e.g. the night of November 11 to 12 is compared with the typical low for November 12.
- Half degrees are counted by rounding down. An exact half degree counts despite floating-point rounding: a deviation of 7.0ºC gives 14 pixels, not 13.
- When the forecast has no high for the displayed day (for example the feed has dropped it) there is no bar; the background and the notch are still shown. When it has no low for the night shown there is no notch.
- The typical highs and lows are compiled into the sketch from the CSVs, which have a row for every day including February 29. After editing a CSV run `bin/make-typical-highs` (which also generates the lows).

### 3.3 Forecast precipitation

If *any* precipitation is forecast for the displayed day, all lit pixels in the top LED strip (the background and the bar; the notch stays dark) perform a "breathing" animation, cycling slowly between maximum and minimum illumination.  The rate of the breathing animation is proportional to the likelihood of precipitation: 100% probability of precipitation is the highest "breathing" speed, while 50% probability of precipitation is 50% of the highest "breathing" speed, and 0% probability of precipitation is no breathing animation used.  A probability under 10% is treated as 0%: NWS reports 2% to mean precipitation is not expected but not impossible, which is best shown as the static no-precipitation display.

#### 3.3.1 Snow or sleet

If any snow or sleet accumulation is forecast for the displayed day, a sparkle animation is added throughout the top LED strip pixels, with random pixels briefly flashing white. A flash may land on any pixel, including the notch.

The top strip always represents today's forecast from midnight until 5pm and tomorrow's from 5pm until midnight (3.4.2), and this sparkle follows that forecast day. (The parking risk is different: it is driven by the hours until the snow is forecast to start, whichever day that is, and has its own sparkle; see 1.2.1.) The feed reports snow accumulation but not sleet, so any measurable forecast snow accumulation triggers it. A flash is full white (54, 54, 54, the luminance-matched white) and fades over 0.4 s; about 8 flashes start per second across the strip (`SPARKLE_PER_SEC`, `SPARKLE_SECONDS`). Flashes are not dimmed with the background (3.1) or by the breathing: they are always full white.

### 3.4 Update frequency

#### 3.4.1 Polling

The National Weather Service API is refreshed once an hour.

#### 3.4.2 Rollovers

The high and the low roll over at different times:

- **5 pm:** the bar, the breathing and the snow sparkle switch from today's forecast to *tomorrow's* (forecast high, probability of precipitation and snow).
- **6 am:** the notch switches from the night now ending, which began the previous evening, to *tonight*, the night beginning this evening.

| Local time | Bar, breathing, sparkle | Night notch |
|---|---|---|
| 00:00–05:59 | Today | The night now ending (began yesterday evening) |
| 06:00–16:59 | Today | Tonight |
| 17:00–23:59 | Tomorrow | Tonight |

So from 6 am to 5 pm the strip reads "today's high, then tonight's low", from 5 pm to midnight "tonight's low, then tomorrow's high", and until 6 am the notch keeps showing the night in progress.
