# street-cleaning-reveaLED

Outdoor LED ambient information display art installation that frames all four sides of a window of roughly 38.5" x 60":
- The two side strips act as bar graphs whose displayed value ranges over 0–90 and is updated hourly, with colors and values that reflect how risky it is to park on that side of the street.
- The bottom strip animates to urge parallel parked cars to the safe side of the street when street cleaning or a snow emergency is imminent, and change color based on trash collection or yard waste collection.
- The top strip represents the National Weather Service forecast high temperature and probability of precipitation for today (or, in the evenings, tomorrow)

Control is by an ESP32 microcontroller running FastLED. Weather, snow emergency, and trash collection schedule is hard-coded for a particular street in Somerville, Massachusetts but could be adapted.

![Elevation sketch](docs/installation-elevation.svg)
