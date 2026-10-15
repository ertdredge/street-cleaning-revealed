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

The top and bottom emphasize the information on the left and right static bar graphs and layer in information about trash collection 

- Parking $risk$ > 55 : Animated pixels marching *away* from highest risk side
  - Speed determined by risk, 55 = slowest, 90 = fastest
- Standard color = Yellow
- After 3pm and...
  - ...trash collected next day, color = Red
  - ...trash and yard waste collected next day, color = Blue

