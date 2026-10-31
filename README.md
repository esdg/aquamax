# AquaMax

AquaMax is being developed as a standalone ESP32-S3 aquarium monitor. This is
an **in-progress implementation**, not yet a complete water-quality monitor.
Phase 1 supports a temperature-only hardware setup; analog sensor integration
and physical verification remain unfinished. The Wi-Fi dashboard includes
temperature history, a temperature calibration workflow, local alerts and a
bounded event journal. The temperature TFT has been visually tested with the
opt-in renderer; the other displays, analog calibration and external
notification integrations remain unfinished.

## Current status

- ESP32-S3 simulation build with 16 MB flash, octal PSRAM configuration and
  two OTA application slots.
- Serial simulation of temperature, pH and TDS, including probe removal and
  restoration. All reported values are explicitly labeled `SIM`.
- Native-tested probe-state debounce, median/moving-average filtering,
  measurement-window scheduling and settings validation.
- Separate DS18B20 and ADS1115 diagnostic builds, with native fake-bus tests.
  The DS18B20 test has been reported working on the physical probe; the ADC
  diagnostic has not yet been tested on hardware.
- The opt-in Wi-Fi dashboard reads the real DS18B20 through a median and moving
  average filter. It shows pH and TDS as not enabled and never powers or reads
  them. Wi-Fi, filtered temperature, time synchronization, dashboard and
  WebSocket fallback have been tested on the board.
- History records available measurements at 10-second intervals after time
  synchronization, with four bounded storage tiers, charting and CSV export.
  The connected DS18B20 is the only source currently enabled.
- The `web_tft` environment renders GC9A01 gauges with a shared SPI bus and
  generated icon masks. Temperature uses the real filtered reading; pH and TDS
  show disabled states. Rendering runs at up to 5 frames per second with a
  provisional 0-40 C temperature scale. The temperature gauge was reported
  visible on one GC9A01 screen; the other screens remain untested. `web_preview`
  still leaves display GPIO unused.
- PH-4502C-style negative-slope calibration and first-calibration-relative slope
  health have native tests and a separate NVS record. The calibration wizard,
  physical ADC integration and real-board NVS persistence remain untested.
- The dashboard provides a stable-reading-gated temperature reference
  adjustment and probe status. Its calibration record has native persistence
  tests; an actual reference calibration and physical NVS persistence still
  need verification with a trusted thermometer.
- Temperature alerts support optional thresholds, hysteresis, debounce,
  repeat limiting and a separate probe-fault alert. Thresholds default to off
  until the owner selects a suitable range. A 100-entry journal stores notes
  and alert transitions; its timestamps are omitted when time is unknown.
- Versioned, checksummed settings storage in NVS with native fake-storage tests.
  Timezone and temperature alert settings persist across reboot on the board.
- Settings and System pages provide timezone selection, diagnostics, a
  versioned configuration backup, confirmed restore, reboot and dual-slot OTA.
  Configuration round-trip, a valid OTA update, rejection of a truncated image
  and reboot were tested on the board. See the limitations below.
- No physical I/O is initialized in the simulation build. The TDS scheduler is
  not connected to a GPIO and must not be used as a hardware safety mechanism.

## Build and test

Install Python and PlatformIO Core. Native tests also require a C++17 compiler
on `PATH`. This Windows machine uses a user-installed WinLibs GCC copied to
`F:\Development\Arduino\AquaMax\winlibs` because the original user-profile
path contains a space that causes its linker to fail. PlatformIO packages are
stored on the development drive because the system drive is low on space.
This machine's ignored `platformio.local.ini` points PlatformIO's package
storage at `F:/Development/Arduino/AquaMax/pio-core`, including when the
VS Code PlatformIO Build and Upload tasks are used. The shared
`platformio.ini` loads this optional local configuration when it exists.

In PowerShell, from the project directory:

```powershell
$env:PLATFORMIO_CORE_DIR = 'F:\Development\Arduino\AquaMax\pio-core'
$env:PATH = 'F:\Development\Arduino\AquaMax\winlibs\bin;' + $env:PATH
python -m platformio test -e native
python -m platformio run -e simulation
```

These paths are local to this development machine; other machines can use
their normal PlatformIO and C++ compiler installation. To configure a local
package directory on another machine, create an ignored `platformio.local.ini`
containing a `[platformio]` section and `core_dir =` followed by that machine's
directory. To flash a confirmed
ESP32-S3 N16R8 board, use `python -m platformio run -e simulation -t upload`;
the simulation has been uploaded and its serial output verified on COM5.
ADC and display operation remain unverified. PlatformIO's
generic board banner may display its N8 defaults even though this environment
overrides the image flash size, partition table and memory type for N16R8.

At 115200 baud, serial commands are `0` (all probes absent), `1` (all present),
`t` (temperature absent), `p` (pH absent) and `d` (TDS absent). Each command
replaces the full simulated presence configuration. Recovery is debounced;
an absent probe immediately stops producing a numeric value.

## Test a DS18B20

The default `simulation` environment does not read GPIO 10. A separate
`temperature_probe` environment reads a real DS18B20 without enabling the
other sensors or displays. Before using it, disconnect USB power and verify
the probe's VDD is connected to 3.3 V, GND to common ground, data to GPIO 10,
and a 4.7 kohm resistor connects data to 3.3 V. Confirm the probe pinout from
its documentation; wire colors are not guaranteed. Do not use 5 V or assume
that the ESP32's internal pull-up replaces the external resistor.

After the wiring is verified, reconnect USB, close any serial monitor that has
the port open, and upload the test environment. On this machine:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e temperature_probe -t upload
```

Open the serial monitor at 115200 baud. `DS18B20 temp_C=-- state=disconnected`
means no valid reading is currently available. Once the device has produced
three valid conversions, the output reports its temperature and
`state=connected`. If it remains disconnected, check power, ground, GPIO
numbering and the pull-up, then inspect the cable and probe. The user has
reported that this environment works with the actual probe. To return to
simulation, upload the `simulation` environment explicitly.

## Test an ADS1115

The separate `adc_probe` environment reads raw A0 voltage at address `0x48`.
It does not read temperature, calculate pH or TDS, or control the TDS supply.
Disconnect USB before wiring and check VDD to 3.3 V, GND to common ground,
SDA to GPIO 8, SCL to GPIO 9, and ADDR to GND. SDA and SCL need pull-ups to
3.3 V; check whether the breakout already provides them. See the
[wiring guide](docs/wiring.md) for voltage limits and pH input protection.

For the first test, disconnect other signals from A0 and connect A0 to GND.
Reconnect USB, close the serial monitor, and upload:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e adc_probe -t upload
```

At 115200 baud, `ADS1115 A0 raw=... volts=...` should report approximately
0 V. A floating input has no meaningful value. `start failed` or `read failed`
indicates an I2C or conversion error; check power, ground, address, SDA, SCL
and pull-ups. This is an ADC communication test, not pH calibration or proof
that a pH probe is connected. Disconnect power before changing the wiring.
After testing, upload `temperature_probe` to restore temperature diagnostics.

## Wi-Fi dashboard preview

This separate preview uses the already-tested DS18B20 wiring on GPIO 10. It
does not initialize the ADC, displays or TDS power switch. The temperature
gauge uses a provisional 0-40 C display range, not configured alert thresholds.
pH and TDS are disabled, not simulated. The dashboard receives snapshots via
the read-only WebSocket on port 81, reconnecting automatically and falling back
to `/api/state` polling every two seconds when needed. HTTP and WebSocket
servicing run on a separate task from sensor sampling. External notifications
and the analog calibration wizards are not yet implemented. Phase 1 analog integration and
the physical display phase remain incomplete.

NTP synchronizes asynchronously after Wi-Fi connects. The default timezone is
Europe/Paris, including daylight-saving changes. The dashboard can select from
seven supported timezones; the selection is saved in NVS. A device without a
successful time synchronization reports unsynchronized time rather than an
invented date. An already synchronized clock continues running during network
loss but is reset on reboot; there is no battery-backed real-time clock.
Internet access is needed for the configured NTP servers, not for the dashboard.

The dashboard appearance can be set to system, light or dark in Settings. The
choice is stored in the browser, not on the device. The home overview uses
live device data; disabled probes and unavailable readings remain clearly
marked rather than displaying example values.

The dashboard includes an offline service worker for secure origins. After an
online visit on HTTPS or localhost has installed it, a subsequent offline
reload can show the cached dashboard shell and its artwork. Measurements and
device actions are not cached; the dashboard shows offline placeholders and
reconnects when the device is available. Browsers do not enable service workers
on the device's ordinary HTTP IP or `aquamax.local` address, so an offline
reload there may show a browser error. HTTPS or a separate trusted proxy is
not provided by this firmware, and the first visit always requires a working
connection.

After startup or reconnection, the temperature reading is held until three
valid conversions establish a connection and five connected conversions fill
the median filter. Expect roughly six seconds plus the dashboard polling delay.
During this warm-up the dashboard shows no numeric reading. A failed sensor
read clears the value and filter immediately; loss is detected when the next
OneWire operation fails. Serial diagnostics at 115200 baud report temperature
and the disabled pH and TDS channels every five seconds.

Build the dashboard and firmware from the project directory:

```powershell
npm ci --prefix web
npm run build --prefix web
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e web_preview
```

To build the dashboard with the optional TFT renderer instead, use:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e web_tft
```

This initializes all three proposed TFT chip selects and the shared reset,
clock, data and DC pins. It does not initialize GPIO 18 or drive `BLK`.
The display electronics and backlight wiring must be checked against the
specific breakout before testing. An unpowered backlight can make a correctly
driven screen appear black. TFT SPI is write-only here, so renderer status
does not prove a screen is connected or showing a correct image.

After the displays initialize, the center pH screen fades the white AquaMax
logo in and out while the temperature and TDS screens stay black. After one
logo cycle, the gauges appear as soon as Wi-Fi connects. Startup does not wait
for any probes. If a previously saved network is offline, the gauges appear
when the Wi-Fi connection attempt times out after 20 seconds, and the existing
setup access point becomes available.

When no Wi-Fi is saved, the center display instead shows the temporary setup
network, password and portal URL after one logo cycle; the other displays stay
black. Once Wi-Fi connects, the gauges appear automatically, even if probes
are disconnected. Sensor and dashboard processing continue throughout. If the
setup access point cannot start, the center display reports the failure;
restart the device to retry.
The setup password is visible to anyone near the display while setup is active.

On the three connected screens, verify the fade, black side screens, readable
setup credentials and portal URL, and the automatic transition to gauges.
Also check a missing temperature probe and an unavailable saved network. These
startup screens and the pH and TDS displays have not yet been hardware-tested.

For firmware-only changes, close the serial monitor and upload the firmware;
this keeps the existing LittleFS files and NVS settings:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e web_preview -t upload
```

Use `web_tft` instead of `web_preview` in that command to retain TFT support.

For a device already running the current firmware and connected to the LAN,
update dashboard assets individually without replacing history:

```powershell
npm run build --prefix web
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" tools/upload_dashboard.py
```

The dashboard build optimizes the supplied banner and generates compressed
logo and image assets. Before uploading these assets to an older installation,
first install firmware containing the new dashboard asset routes and upload
allowlist. Firmware-only upload preserves the existing LittleFS history and NVS
settings. Uploading the new dashboard to older firmware will fail.

The uploader defaults to `http://aquamax.local`; use `--url http://DEVICE_IP`
when mDNS is unavailable. It requires the current firmware's asset endpoint.
Asset updates and the dashboard use unauthenticated HTTP on a trusted LAN.

## Settings and system

The System page reports live memory, network and storage diagnostics. A
configuration backup contains the timezone, temperature calibration and
temperature alert settings. It does not contain Wi-Fi credentials, history,
journal entries or dashboard files; keep a separate LittleFS backup for those.
Restore requires confirmation and validates the entire file before writing.
The three settings records are written separately: a storage or power failure
can leave a partial restore, which must be checked and retried. Restoring a
calibration to a different probe can give misleading readings; verify it with a
trusted thermometer. Restoring alert settings can enable saved thresholds.

Restart and OTA first attempt to flush buffered history and refuse to proceed
if that flush fails. OTA accepts an ESP32-S3 application `.bin` up to the
inactive 3 MB slot, checks the image using the ESP-IDF OTA APIs and leaves
LittleFS and NVS alone. It does not update dashboard assets. Use only a build
made for this device and partition layout. Image validation is not firmware
authentication, and automatic rollback after a valid but broken application
boots is not implemented. Keep serial access available for recovery.

There is no password, TLS or firmware signature check on these endpoints.
Same-origin checks are not authentication: anyone on the trusted LAN can
change settings or install firmware. Never expose the device to an untrusted
network or the Internet.

The dashboard supplies an install manifest and a service worker for secure
origins. Browsers generally do not consider `http://aquamax.local` secure, so
installation, offline caching and browser notifications may be unavailable
there. The device does not currently serve HTTPS. Where supported, only static
dashboard assets are cached; live readings and API responses are never cached.
The current Settings page does not yet configure device name, units, theme,
display settings, external export or network credentials; Wi-Fi provisioning
uses the setup access point described below.

For a first installation only, `uploadfs` can initialize the asset filesystem.
**It replaces the entire LittleFS partition, including all history and other
files.** Back up any data you need first. It does not erase NVS settings.

For this machine and the current partition layout, a raw partition backup can
be made after closing the serial monitor (use the actual serial port):

```powershell
$backup = Join-Path $env:USERPROFILE ("aquamax-littlefs-{0}.bin" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))
& "$env:USERPROFILE\.platformio\penv\Scripts\python.exe" 'F:\Development\Arduino\AquaMax\pio-core\packages\tool-esptoolpy\esptool.py' --chip esp32s3 --port COM5 read_flash 0x610000 0x9e0000 $backup
```

Verify that the backup succeeded before replacing the filesystem. The backup
contains everything in LittleFS and should be kept private. The offsets are
specific to the checked-in partition table; revisit them if it changes.

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e web_preview -t uploadfs
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e web_preview -t upload
```

Open the serial monitor at 115200 baud and press reset if the startup message
was missed. On first boot, connect a phone or computer to the
`AquaMax-Setup-...` network using the **temporary setup password printed in the
serial monitor** (also shown on the center screen with `web_tft`). Visit
`http://192.168.4.1/setup` and enter the name and
password of a 2.4 GHz Wi-Fi network. Return the phone or computer to that same
network and open the IP address printed in the monitor, or try
`http://aquamax.local`. The setup access point closes 60 seconds after a
successful connection. It can briefly disconnect when the ESP32 joins your
network; reconnect to it to check the setup page, or look for the dashboard IP
in the serial monitor. If connection fails, return to setup and retry; the
previous saved network is not replaced until a connection succeeds. The serial
monitor reports a disconnect reason code without printing the Wi-Fi password.
`NO_AP_FOUND` can indicate the wrong network name, an unavailable 2.4 GHz
network or a weak signal; `AUTH_FAIL` or `HANDSHAKE_TIMEOUT` can indicate a
password or security mismatch. Try WPA2 on 2.4 GHz if your router is WPA3-only.
The access-point password printed at startup is temporary and should not be
shared when reporting logs.

Wi-Fi credentials are stored in the ESP32's NVS and are not returned by the
API or printed in the serial monitor. The temporary setup password is printed
so you can join the setup network. Both setup and dashboard use **unencrypted
HTTP** and the dashboard has no authentication; use only a trusted local
network and never expose the ESP32 to the internet. NVS encryption is not
configured. The setup page is accessible only through the temporary access
point, which has a randomly generated password; it is not a substitute for
TLS. The dashboard, temperature, WebSocket streaming and HTTP fallback have
been tested on the ESP32.

`GET /api/state` and `ws://aquamax.local:81/ws` return temperature value or
`null` and its probe state, disabled pH and TDS values, Wi-Fi connection state,
uptime and time synchronization. Browser WebSocket connections and timezone
changes are restricted to the device's own HTTP origin. The API contains no
network credentials. The dashboard filesystem contains generated, gzip
compressed assets; rebuild and upload the individual assets after changing the
web source. To return to the previous test, upload `temperature_probe`
explicitly.

## History

The dashboard provides 1 hour, 24 hour, 7 day, 30 day and 1 year views. The
fixed-size tiers retain 24 hours at 10-second resolution, 7 days at 1-minute
resolution, 30 days at 5-minute resolution and 1 year at 1-hour resolution.
Records contain a timestamp, minimum, weighted average, maximum and sample
count. Missing readings remain gaps; pH and TDS do not contain invented data.

History is sampled every 10 seconds only after the device has synchronized its
clock. Readings before synchronization are omitted rather than assigned an
invented time. Buffered records are written approximately every five minutes,
so an unexpected power loss or reboot can lose up to that much recent history.
Checksums allow invalid or torn records to be skipped, but they are not a
substitute for a backup. The storage indicator reports overall LittleFS usage,
including dashboard files, not history alone.

Charts and CSV exports aggregate each selected range to about 240 points.
CSV timestamps are UTC Unix seconds; rows with no samples are omitted. The
dashboard can clear all history after confirmation; this deletes history files
only, not Wi-Fi settings or dashboard assets. History clearing and dashboard
asset uploads are restricted to the device's HTTP origin, but these checks are
not authentication. Do not expose the device to untrusted networks.

## Calibration and probe health

Open Calibration on the dashboard to see the temperature probe status, last
valid reading and current offset. Place a trusted reference thermometer next
to the probe, wait for both to stabilize, enter the reference temperature in
degrees Celsius and select Save calibration. The device requires eight fresh
conversions spanning no more than 0.3 C, and accepts an offset within +/-5 C.
The reference is compared against the filtered uncalibrated reading, not the
previously adjusted reading. Reset requires confirmation and restores zero
offset. Changes affect subsequent live readings and history; earlier history
is not recalculated.

Temperature calibration is stored in a separate checksummed NVS record. Its
date is recorded when the device clock is synchronized; otherwise the date is
unavailable. The dashboard reports storage errors rather than claiming a save
succeeded. Calibration writes are restricted to the device's HTTP origin and
rate-limited, but HTTP is unauthenticated and must remain on a trusted LAN.
Use a real reference thermometer: no calibration has been applied to the
connected probe during development testing.

pH and TDS remain disabled, and their calibration controls are unavailable
until the analog channels and actual modules are integrated and verified.
Native-tested pH slope and temperature-compensation calculations are
foundations only; they do not enable the ADC or generate measurements. Probe
age and slope trends for analog probes are not yet available.

## Alerts and journal

Temperature threshold alerts are disabled by default; configure a suitable
range for the actual aquarium before enabling them. Profile choices are only
starting points, not guarantees of suitability. Probe-fault alerts are enabled
by default. Hysteresis and debounce reduce chatter, and repeats are limited by
the configured interval. Missing or disabled readings do not generate numeric
threshold alerts; a fault recovers only after valid readings return. Alert
settings are stored in a separate checksummed NVS record. A storage error is
shown rather than silently enabling default threshold values.

The journal retains the most recent 100 entries, including notes and alert
transitions. Entries with unknown device time have no timestamp; they are not
plotted on the history chart. Deleting a journal entry does not delete history
or dashboard assets. Clearing measurement history does not clear the journal.
Tips currently cover temperature alerts, probe faults and dated temperature
calibration only; no analog readings or advice are fabricated.

Browser notifications require explicit permission, a browser-supported secure
origin and an open dashboard; plain `http://aquamax.local` is generally not a
secure origin. WhatsApp, Telegram, webhook and MQTT delivery are **not yet
implemented**, and no external alert delivery should be assumed. The local
dashboard is not a substitute for an independent aquarium safety system.

## Phase 2: Display foundations

The gauge model clamps its animated cursor to a caller-provided range, hides
readings immediately on missing or faulty probes, and does not invent a value
after a fault. Gauge ranges and display appearance still need integration with
settings and the renderer.

The PlatformIO pre-build script checks the seven original SVG files and the
generator against the fingerprint in [src/generated/icons.h](src/generated/icons.h).
The generated header contains 32 px and 48 px antialiased alpha masks, including
the SVG mask gaps. It must not be edited manually. When the SVGs or generator
change, install Node.js and run `npm ci --prefix web` before building firmware;
the header is regenerated automatically. With unchanged inputs, firmware
builds use the existing header without Node.js or Python imaging packages.
Set `AQUAMAX_ICON_SIZES` to unique comma-separated pixel sizes from 8 to 128
to regenerate different sizes; the current tests and planned renderer use the
default 32 and 48. Run `npm run test:icons --prefix web` to compare the default
generated masks against their source SVGs.

The display pins are provisional. Confirm the GC9A01 breakout model, supply,
backlight wiring and shared SPI connections before any hardware display test.
The separate detailed gauge design specification is still needed for an exact
visual match. This work has not been uploaded to the board.

## Provisional wiring: do not connect yet

See the [module-by-module wiring guide](docs/wiring.md) for the full pinout,
GX16 connections and pre-power checks.

Pin assignments in [include/config/pins.h](include/config/pins.h) are a
proposal, not verified wiring. Check the actual board revision, module models,
voltage ranges, backlight current and cable pin numbering before use.

| Function | Proposed GPIO |
| --- | --- |
| TFT SCLK, MOSI, DC, reset | 12, 11, 13, 14 |
| TFT CS: temperature, pH, TDS | 15, 16, 17 |
| ADS1115 SDA, SCL | 8, 9 |
| DS18B20 data | 10 |
| TDS power-switch control | 4 |
| Optional cable detect | 5 |
| Optional backlight driver | 18 |

The proposed GX16 connections are: 1 pH module +5 V, 2 DS18B20 +3.3 V,
3 switched TDS supply, 4 pH output, 5 TDS output, 6 OneWire data, 7 common
ground, 8 cable detect tied to ground in the probe enclosure. Confirm connector
orientation and numbering with a continuity test. Use a shielded cable with the
shield grounded at the main enclosure end only. The optional cable-detect line
is disabled by default; it detects the cable, not individual analog probes.

The proposed TDS supply is a **high-side** switch, never a ground switch. An
active-high driver for a P-channel MOSFET is assumed; use an external pull-down
on the driver input and a gate pull-up so it remains off during boot. The
module must support the switched supply voltage. Confirm GPIO drive, switch
current, inrush and module warm-up before powering it. Direct GPIO power is not
approved without electrical verification. No GPIO currently controls the switch.

Power the ADS1115 and its I2C pull-ups from 3.3 V. An ADC gain setting does not
protect inputs from excessive voltage. For a confirmed 0-5 V pH output, a
provisional 12 kohm upper / 18 kohm lower divider gives 0.6 times the module
voltage, or 3.0 V at 5.0 V; a 100 nF capacitor filters A0 to ground. Verify tolerances, transients, impedance and
settling before wiring; check the TDS output independently. A DS18B20 normally
needs an external pull-up, provisionally 4.7 kohm to 3.3 V, verified against
the actual cable. Backlights may require a separate suitable driver.

## Assumptions and outstanding work

- The pH board is PH-4502C-style with negative-slope `Po`, but its exact gain
  and signal limits are unknown. The TDS module model and transfer curve remain
  unknown. Real pH or TDS values require confirmed hardware and calibration.
- The design reference image has been supplied in the conversation; the
  separate design specification is still pending. No display renderer exists.
- Freshwater-community settings, Celsius, Europe/Paris and local history are
  planned defaults, not implemented settings yet.
- The final dashboard is intended for trusted local HTTP only, without admin
  authentication. LAN traffic will be unencrypted. Never expose it directly to
  the Internet. Input validation, secret redaction and confirmation of
  destructive actions remain required.
- Native tests and a cross-compiled binary do not verify electrical safety,
  NVS persistence, PSRAM, flash partitions, real sensor quality or hardware
  timing. These require the actual board and probes.