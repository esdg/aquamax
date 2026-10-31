# AquaMax – Agent Build Brief (Firmware + Dashboard)

Prompt for the coding agent in VS Code. Build the complete firmware and the embedded web dashboard for AquaMax, an ESP32-S3 aquarium water-quality monitor. Work phase by phase (section 13), build and verify each phase, and list every assumption you make.

## 1. Mission

Build a standalone Wi-Fi water-quality monitor that measures temperature, pH and TDS in real time. It shows the three values on three round TFT gauges and serves a polished web dashboard (stored on the ESP itself) with live data, history, a guided calibration wizard, alerts, tips and settings. It must work without Internet and without any external server. External services (MQTT, push notifications) are optional add-ons.

## 2. Hardware

- MCU: ESP32-S3 N16R8 (16 MB flash, 8 MB octal PSRAM), board ESP32-S3-DevKitC-1, powered over USB.
- 3x GC9A01 1.28 inch round TFT, 240x240, SPI, 3.3 V: one gauge each for temperature, pH and TDS. Shared SCLK, MOSI, DC and RST, one CS per screen. Backlight pin should be PWM-dimmable if wired.
- ADS1115 16-bit I2C ADC. A0 = pH module output, A1 = TDS module output, A2/A3 spare (use one for supply-voltage monitoring if practical).
- pH module (range 0-14, BNC probe, analog output).
- TDS module (0-1000 ppm, analog output).
- 1x DS18B20 waterproof probe, placed in the water, on the OneWire pin (only one temperature probe is used).
- Probes live in a separate enclosure linked to the main enclosure by a cable; expect cable noise and keep the code tolerant (filtering, error detection). The link is an 8-pin GX16 connector. Proposed pinout (confirm against the actual wiring): +5 V (pH module), +3.3 V (DS18B20 probe), TDS module supply driven directly by a GPIO (the module draws only 3 to 6 mA, which also gives the measurement-window power gating for free), pH analog output, TDS analog output, OneWire data, common GND, and one spare pin (cable-detect line, recommended). Assumptions: the ADS1115 sits in the main enclosure, and the pH and TDS modules sit in the probe enclosure next to their probes (the pH probe must stay close to its module). Use a shielded cable, with the shield tied to GND at the main enclosure end only.

Pin rules for ESP32-S3 N16R8: do not use GPIO 26-37 (flash/PSRAM), avoid strapping pins (0, 3, 45, 46) and the native USB pins (19, 20). Put every pin in one `config/pins.h` and document the wiring in the README. Verify the ADS1115 input limits: with a 3.3 V supply, inputs must not exceed about VDD + 0.3 V, so use a voltage divider if the pH module outputs up to 5 V (make the divider ratio a constant).

## 3. Toolchain and project structure

- PlatformIO in VS Code, Arduino framework, C++17, board `esp32-s3-devkitc-1`, PSRAM enabled (octal), 16 MB flash, a custom partition table with two OTA slots plus a large LittleFS partition.
- Suggested libraries (confirm current versions and maintenance status): LovyanGFX (preferred over TFT\_eSPI for multi-display and sprites), an ADS1115 library, OneWire plus DallasTemperature, ESPAsyncWebServer (maintained fork) with AsyncTCP, ArduinoJson, PubSubClient (MQTT), WiFiClientSecure for HTTPS, Preferences (NVS) for settings.
- Folder layout: `src/` split into modules (`sensors`, `calibration`, `display`, `network`, `webserver`, `history`, `alerts`, `notifications`, `tips`, `settings`, `system`), `include/config/`, `data/` (built dashboard, gzipped), `web/` (dashboard source), assets/icons (SVG icons) and tools (build-time scripts such as the icon converter), `test/` (native unit tests), `docs/`.
- Keep modules decoupled through small interfaces so relays, heater, lighting or feeder modules can be added later without refactoring.
- Add a **simulation mode** (compile flag or runtime setting) that generates realistic fake sensor data so UI and display can be developed without probes.

## 4. Runtime architecture

- FreeRTOS tasks: sensor+display on one core, Wi-Fi, web server, history and notifications on the other. The web server must never block the gauges.
- Sensor task: sample, filter, compensate, publish the latest readings and trend in a shared thread-safe structure.
- Display task: render at a stable frame rate, driven by the shared readings.
- Notification and external-export tasks must be non-blocking with timeouts and retries.
- Enable the task watchdog, handle low-memory and sensor faults gracefully, and expose uptime, free heap, free PSRAM, reset reason and RSSI in the system page.

## 5. Sensors and measurement quality

- **Temperature (DS18B20):** asynchronous 12-bit conversion, CRC check, reject disconnected (-127) and power-on (85.0) values, calibration offset.
- **ADC reading:** ADS1115 single-shot or continuous at a modest sample rate, programmable gain chosen for the real voltage range, oversampling, median plus moving-average filter, outlier rejection.
- **pH:** convert voltage to pH with a two- or three-point linear calibration (4.01 / 6.86 / 9.18 buffers), apply temperature compensation (Nernst slope) using the water temperature, clamp to 0-14, report slope percentage and offset.
- **TDS:** convert to conductivity with compensation (about 2 percent per degree C around 25 C), then to ppm with a configurable factor (default 0.5), calibrated against a 1413 uS/cm standard.
- **Probe interference:** the TDS probe injects current into the water and disturbs the pH reading. Power the TDS module from a GPIO only during its measurement window (for example 1 s every minute), read pH while TDS is off, and keep the TDS value latched between measurements. Make the schedule configurable.
- **Fault detection:** flag each channel as OK, stale, out of range or disconnected, and never show an invented value.
- Compute a short-term trend (rising, falling, steady) per measurement, with hysteresis to avoid flicker.
- Calibration data, offsets and factors are stored in NVS and survive reboots and OTA updates.

### Probe presence and fault handling

Every sensor channel has an explicit connection state, and the whole system (screens, dashboard, history, alerts, calibration) must behave correctly when a probe is missing, unplugged at runtime, or plugged back in. Never show or store an invented value.

- States per channel: `connected`, `disconnected`, `unstable` (noisy or erratic), `out_of_range`, `disabled`. Debounce transitions (several consecutive bad samples before declaring a fault, several good ones before recovering) and recover automatically on hot-plug, without a reboot.
- DS18B20: scan the OneWire bus at boot and periodically. A probe is disconnected when its ROM address stops answering, the CRC fails, or reads return -127 or the 85.0 power-on value repeatedly. Re-scan to detect a reconnected or replaced probe, and adopt the new probe's address automatically (only one probe is used).
- pH and TDS (analog): presence cannot be sensed reliably from the signal alone. Combine heuristics (reading pinned near a rail or 0 V, implausible value, abnormal noise or variance) and use the spare connector pin as a cable-detect line (tied to ground in the probe enclosure, read by a GPIO with internal pull-up): with a single DS18B20, its silence cannot tell an unplugged cable from an unplugged temperature probe. Make the detect line optional and configurable, and document the wiring. It only proves the cable is plugged in, not that a probe is attached to its module. When the cable is detected as unplugged, flag all three channels as disconnected. A TDS probe in pure water or in air legitimately reads near zero, so treat near-zero TDS as a fault only when the detect line or other heuristics agree.
- Per-channel enable/disable in settings (for example a pH or TDS probe not installed yet). A disabled channel is greyed or hidden in the UI and raises no alerts.
- Temperature compensation: if the water temperature probe is disconnected, never silently reuse a stale value. Fall back to a configurable default temperature (25 C), flag pH and TDS as `compensation: default temperature` in the API, and show a warning in the UI.
- Screens: use the probe absent or error design from the reference image (see section 6). Each screen reacts independently.
- Dashboard: a live status chip per probe (connected, disconnected, unstable, disabled) on Home and in probe health, with last-seen time and clear hints on what to check (cable, connector, probe). The web gauge of a missing probe uses the same greyed-out design as the screens.
- History: record gaps while a probe is disconnected, never zeros or stale values. Charts show the gap, and min/avg/max ignore it.
- Alerts: a dedicated probe-disconnected alert with its own message, anti-spam and recovery message. Suppress threshold alerts for a channel while it is disconnected.
- Calibration: block the wizard for a channel that is not connected or not stable, with a clear explanation.
- Boot: the device must start, drive the screens and serve the dashboard even with zero probes connected.
- Simulation mode must be able to simulate probe disconnections to test all of the above.

## 6. Round displays (3x GC9A01)

Reproduce the supplied reference design exactly (the gauge reference sheet and the design specification doc).

- Near-black background. Continuous gradient arc of about 270 degrees with the opening at the bottom, and a thin glowing outer rim. Gradients: temperature blue, cyan, green, orange, red; pH purple, blue, cyan, green, orange, red; TDS blue, green, orange, red.
- A small glowing white tick is the cursor and slides smoothly along the arc (easing, no jumps).
- Center: an icon (thermometer, one drop, three drops), the value in large light digits, the unit below, then the measurement name.
- Value colors by measurement: cyan for temperature, violet for pH, amber for TDS.
- Bottom: status dot (green, orange, red) and a trend arrow.
- States: normal; alert (red halo around the dial, orange value and dot); probe absent or error, exactly as in the reference image: the whole dial greyed out (flat grey arc with no gradient, no glow and no cursor, faint tick marks), the measurement icon in light grey with a diagonal slash through it, the value replaced by two grey dashes, the unit in light grey, the measurement name below, and a red warning triangle with an exclamation mark at the bottom in place of the status dot and trend arrow. Same treatment for pH and TDS, each with its own slashed icon (drop, three drops) and unit.
- Performance: pre-render the static layer (gradient, rim, ticks) once into a PSRAM sprite per gauge; redraw only the cursor, value, dot and arrow with partial updates or a double-buffered sprite and DMA. Target a smooth 30 fps for animation and about 5 Hz for value changes, with no flicker or tearing.
- Arc range and the position of the green zone are driven by settings (aquarium profile), not hard-coded. Icons come from the supplied SVGs (see the Icons subsection below), and a single clean font is used for the digits.
- Settings: brightness and screen rotation (no ambient light sensor, so no automatic or night dimming).

### Icons (SVG assets)

- **Source of truth:** `assets/icons/*.svg`, supplied by the user: `thermometer`, `drop`, `three-drops`, `warning`, and the slashed missing-probe versions `thermometer-off`, `drop-off`, `three-drops-off`. Each is a 48x48 viewBox, single color, filled paths using `currentColor`. The slashed versions use an SVG `<mask>` for the gap around the slash. Do not redraw or restyle them. If the files are missing from the repo, stop and ask the user for them.
- **Screens (build-time conversion):** add a PlatformIO pre-build script (`extra_scripts`, Python) that rasterizes each SVG (for example with cairosvg, so masks are honored) into 8-bit alpha masks at the sizes the gauges need (32 px and 48 px, configurable) and writes C arrays to `src/generated/icons.h`. Commit the generated header so the firmware builds without the Python dependencies, and regenerate it only when an SVG changes. Never hand-edit it.
- **Runtime tinting:** draw the masks with the color of the current state (cyan temperature, violet pH, amber TDS, light grey for the missing-probe state, red for the warning triangle), blending with the alpha channel for clean anti-aliased edges. Pre-composite the icon into the static sprite whenever the state does not change.
- **Web dashboard:** use the same SVGs directly (inlined in the bundle as symbols or components), colored with CSS `currentColor` per state, so the dashboard and the screens share one set of icons.
- Trend arrows and status dots are drawn in code and need no SVG.

## 7. Network and server

- Wi-Fi station mode with a captive-portal provisioning flow on first boot, automatic reconnect, and an access-point fallback if the saved network is unavailable.
- mDNS at `aquamax.local`, NTP with the Europe/Paris timezone (configurable), and OTA firmware update from the dashboard.
- Dashboard files are served from LittleFS, gzip-compressed, with sensible cache headers. No CDN or external requests: the dashboard must work with no Internet.
- Real-time data over WebSocket (or SSE), with automatic reconnect on the client.
- Optional access protection: an admin password or token required for settings, calibration and OTA. Never return stored secrets (API keys, Wi-Fi password) in API responses.

### REST and WebSocket API (proposal, refine as needed)

- `GET /api/state` current readings, trends, statuses.
- `GET /api/history?metric=&range=` aggregated points (min, avg, max) for 1h, 24h, 7d, 30d, 1y. Also `GET /api/history/export.csv`.
- `POST /api/calibration/{ph|tds|temp}/...` wizard steps: start, capture point, confirm, cancel, reset.
- `GET/PUT /api/settings`, `GET/PUT /api/alerts`, `POST /api/notifications/test`.
- `GET/POST/DELETE /api/events` journal entries.
- `GET /api/system`, `POST /api/system/reboot`, `POST /api/system/ota`, `GET/POST /api/system/config` (backup, restore).
- `WS /ws` pushes live readings and alert state.

## 8. History and storage (user-selectable)

The user chooses the storage mode in the dashboard settings:

- **Local only (default):** multi-resolution history stored on the ESP in LittleFS.
- **Local + external export:** same local history, plus optional push to MQTT, InfluxDB (HTTP line protocol), or a generic webhook. Configurable interval, retention and credentials.

Local design (round-robin style, per measurement, storing min, avg and max per bucket): 10 s resolution for 24 h, 1 min for 7 days, 5 min for 30 days, 1 h for 1 year. Use compact fixed-size binary records (scaled 16-bit integers plus a timestamp), buffer in RAM and flush in batches every few minutes to limit flash wear, and keep a consistent state after power loss. Provide a way to clear history, show storage usage and retention, and handle time not yet synced (queue or flag samples until NTP is available).

## 9. Calibration wizard

- pH: guided 2- or 3-point calibration (4.01, 6.86, 9.18). Show the live voltage and value, detect stability automatically (low variance over a time window) and enable the Capture button only when stable. Warn about temperature of the buffer, rinsing the probe between buffers, and out-of-range slope.
- TDS: single-point calibration with a 1413 uS/cm standard, with temperature compensation, plus a configurable conversion factor.
- Temperature: calibration offset.
- After each calibration, save results with a date, show slope percentage and quality (good, acceptable, replace probe), and allow reset to factory defaults.
- Probe health page: calibration age, slope trend, drift, and a replace-soon warning when slope falls below about 85 percent.

## 10. Dashboard (web UI)

Single-page app built with Vite and Preact (or vanilla TypeScript), output gzipped into `data/`. Keep the total payload small (target under 300 KB gzipped) and use uPlot for charts. Mobile-first, installable PWA, dark theme by default plus light theme, accessible (contrast, keyboard, ARIA, status never conveyed by color alone), with proper loading, empty and error states. The visual language must match the TFT gauges (same gradient arc gauges rendered in SVG, same icons and accent colors).

Pages (bottom bar on mobile, sidebar on desktop, persistent status chip for Wi-Fi and alerts):

- **Home:** three live gauges with trend and status, a 24 h sparkline under each, active alert banner, tip of the day.
- **History:** ranges 1h / 24h / 7d / 30d / 1y, overlay or separate charts, min/avg/max, zoom, tooltips, journal events as annotations, CSV export.
- **Calibration:** the wizard from section 9 with step progress and stability indicator.
- **Tips and health:** probe health, contextual advice, event journal (water change, product added, notes).
- **Alerts:** aquarium profile presets, per-measurement min and max thresholds, hysteresis, anti-spam delay, channel configuration, test button.
- **Settings:** Wi-Fi, device name, timezone, units, theme, screen settings (brightness, gauge ranges), history storage mode and retention, MQTT / Home Assistant, security.
- **System:** diagnostics, OTA update, config backup and restore, reboot.

## 11. Alerts, notifications and tips

- **Profiles** (examples): freshwater community, shrimp, planted, discus. Each presets thresholds for temperature, pH and TDS, and the position of the green zone on the gauges. Users can edit them.
- **Alert logic:** per-measurement min and max thresholds, hysteresis, debounce, anti-spam delay between repeats, a recovery message when values return to normal, and distinct handling for sensor faults.
- **Channels** (user-selectable, any combination): WhatsApp through CallMeBot, Telegram bot, in-page browser notifications (while the dashboard is open), a generic webhook, and MQTT with Home Assistant discovery. Send over HTTPS from a non-blocking task with timeouts and retries. Verify the current CallMeBot API and its limits before implementing; keep the channel layer pluggable so a provider can be swapped without touching alert logic. No GSM modem is used.
- **Tips engine:** simple rule-based advice, for example fast pH swings, temperature swings, TDS rising since the last water change, a probe overdue for calibration. Rules are data-driven and easy to extend.
- **Event journal** stores timestamped notes, shown on the History charts.

## 12. Quality requirements

- Clean, commented, modular C++ with consistent naming; no magic numbers (constants in config headers); no blocking delays in tasks.
- Native unit tests (PlatformIO `native`) for calibration math, compensation formulas, filtering, trend detection, downsampling and alert hysteresis.
- Robustness: sensor faults, Wi-Fi loss, NTP failure, flash errors and low memory are all handled and surfaced in the UI, never silently ignored.
- Security basics: validate and bound every API input, rate-limit sensitive endpoints, store secrets only in NVS, never log them.
- Documentation: README with wiring table (pins, divider values), build and flash steps, how to build and upload the dashboard (`uploadfs`), configuration reference and API reference.
- Out of scope for now: relays, heater, lighting, feeder, ATO, leak detection. Design the module system so they can be added later.

## 13. Delivery phases

Work in this order, building and testing each phase before moving on, and summarize what was done and what is assumed at the end of each phase.

- **Phase 1:** project skeleton, config and pins, settings in NVS, sensors (DS18B20 and ADS1115), filtering, probe presence detection, simulation mode, serial diagnostics.
- **Phase 2:** the three GC9A01 gauges with the full design, animation and states.
- **Phase 3:** Wi-Fi provisioning, mDNS, NTP, web server, WebSocket, and the Home page with live gauges.
- **Phase 4:** history engine (multi-resolution storage, API, History page, CSV export) and the storage-mode setting.
- **Phase 5:** calibration wizard, probe health, and compensation.
- **Phase 6:** alerts, notification channels (WhatsApp via CallMeBot, Telegram, webhook, MQTT), tips engine and event journal.
- **Phase 7:** Settings and System pages, OTA, backup and restore, PWA, accessibility and performance polish, documentation.

Before starting, restate your understanding, list open questions (for example exact GPIO assignments, pH module output voltage, backlight wiring), and propose sensible defaults so work is never blocked.
