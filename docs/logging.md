# Logging to Grafana Cloud Loki

`logMsg(level, fmt, ...)` (in `loki-log.h/.cpp`) replaces every `Serial.printf` / `Serial.println` log call in the sketch.
It prints to Serial immediately, then queues the line for Loki.

- **Never blocks the serial side.** `logMsg` only formats, prints and copies the line into a 24-line ring. The HTTPS POST is made by `lokiFlush()`, called from the weather task (core 0) every 5 s. A slow or failed POST (8 s timeouts) delays only that task. If the ring overflows, the oldest queued lines are dropped from the Loki queue (not from Serial) and a warning line records how many.
- **Levels** (stream label `level`): `debug` for the verbose render dump (`now`, `top:`, `snow:`, `waste:`, bar speeds); `info` for command acknowledgements, status and forecast reports, fetch successes; `warn` for fetch failures, usage errors and bad input. `error` exists but nothing uses it yet. Stream label `app` is `street-cleaning-reveaLED`.
- **Timestamps.** Loki's push API requires a timestamp on every line, so none can be omitted. Lines logged before NTP succeeds wait in the queue; once the clock is valid they are sent with a timestamp computed as (current real time − how long ago they were logged, from `millis()`). If NTP never succeeds, nothing is sent. The timestamp uses `realNow()`, so the simulated clock (`time`, sweeps, `test-suite`) does not skew it. Lines longer than 255 characters are cut in Loki (Serial gets up to 383).
- **Failures.** Backoff 15 s, 30 s, 60 s, 120 s, then 300 s between attempts, keeping the batch until it succeeds, except HTTP 400/413/422 (the batch itself is refused), which drops the batch. The sender's own messages (`loki: ...`) are Serial only, so they cannot feed back into the queue.
- **Security.** The connection uses `setInsecure()` (no certificate check), as the NWS fetch does, so the token could be captured by a man in the middle on your network. Use a token scoped to `logs:write`. To fix, pin the Grafana Cloud root CA with `setCACert` (TODO in `loki-log.cpp`).
- **secrets.h** needs `LOKI_URL` (base, e.g. `https://logs-prod-XXX.grafana.net`, a trailing slash is tolerated), `LOKI_USER`, `LOKI_TOKEN`.

Query in Grafana Explore: `{app="street-cleaning-reveaLED"}`, or `{app="street-cleaning-reveaLED", level="warn"}`.
