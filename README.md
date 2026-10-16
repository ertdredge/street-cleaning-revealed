# street-cleaning-reveaLED

Outdoor LED art installation that frames a window of roughly **38.5" x 60"*. The top and bottom strips animate to urge parallel parked cars to the safe side of the street when street cleaning is imminent, and change color based on trash collection or yard waste collection. The **two side strips act as bar graphs** whose displayed value ranges over **0–90** and is **updated hourly**, with colors and values that reflect how risky it is to park on that side of the street. Control is by an **ESP32 microcontroller running FastLED**. In a later phase the controller will use **WiFi and HTTPS** to monitor a web page for keywords and augment the 0–21 values with information gleaned from there.

![Elevation sketch](docs/installation-elevation.svg)
