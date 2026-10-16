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

- $percent$ forecast of snow
- $inches$ of snow forecasted
- $hours$ until start of snow

If $percent$ is 100% and $inches$ forecast is >=4 we use determine risk with the street cleaning function above, and the number of hours until the snow is forecast to start is used for $remaining$. So it acts as if there was scheduled street cleaning *ending* at the time the snow is forecasted to *begin*. If the city declares an actual snow emergency then the start of the parking ban is used to calculate $remaining$ instead of start of snow forecast.

If inches forecast is < 4 then the risk function result is multiplied by $\frac{inches}{4}$. If $percent$ is less than 100% then the risk function result is multipled by $percent$.

---

## 2. Top and Bottom

The top and bottom emphasize the information on the left and right static bar graphs and layer in information about trash and yard waste collection. Both strips always show the same color and animation.

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

Red, green and yellow mean "don't" and "safe" and are reserved for parking on the left and right bar graphs, so the top and bottom never reuse them. The colors here avoid every hue the bar graphs use (green at 118° through amber to red at 0°). Azure is pure blue plus a little green, which gives it a bit more brightness than pure blue would have.

The top and bottom show a pattern of "ants" (2 lit pixels out of every 6) on a background. For white, azure and pink the background is the same color dimmed. For yard waste the ants are azure, the same color as a trash-only week, on a spring green background, because trash and yard waste both go out. The earlier pairing of two close greens that differed only slightly in hue was too hard to see marching. The new pair differs in both hue (228° against 150°) and brightness (azure is roughly twice as bright), which makes the ants easy to pick out.

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
