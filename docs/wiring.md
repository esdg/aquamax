# AquaMax wiring guide

## Before connecting anything

This is a **proposed pinout**, not a verified wiring diagram. The pH board is
PH-4502C-style, but its exact output swing is not yet verified. The TDS module,
screen backlight circuits and board revision are also unconfirmed. The current firmware runs in simulation mode:
it does not read the physical sensors, drive the displays or switch TDS power.

Do not power or connect the modules until their datasheets and voltages have
been checked. Disconnect USB power while changing connections. GPIO numbers
below are ESP32-S3 **GPIO labels**, not positions on the physical header.
Check your particular board's pin labels and schematic.

All module grounds, the ADS1115 ground and the ESP32 ground share a common
ground. Use 3.3 V logic for the ESP32, ADS1115 and TFT signal pins. Never
connect a 5 V signal directly to an ESP32 GPIO or a 3.3 V-powered ADS1115.

## ESP32-S3 connections

| ESP32-S3 pin | Connect to | Notes |
| --- | --- | --- |
| GPIO 12 | SCLK / SCK on all three TFTs | Shared SPI clock. |
| GPIO 11 | SDA / MOSI / DIN on all three TFTs | Shared SPI data to the screens; not I2C SDA. |
| GPIO 13 | DC / A0 on all three TFTs | Shared display data/command signal. |
| GPIO 14 | RST / RES on all three TFTs | Shared display reset. |
| GPIO 15 | CS on the temperature TFT | Only the temperature screen. |
| GPIO 16 | CS on the pH TFT | Only the pH screen. |
| GPIO 17 | CS on the TDS TFT | Only the TDS screen. |
| GPIO 8 | SDA on the ADS1115 | I2C data. |
| GPIO 9 | SCL on the ADS1115 | I2C clock. |
| GPIO 10 | DS18B20 data via GX16 pin 6 | Add a pull-up to 3.3 V; see below. |
| GPIO 4 | TDS high-side switch driver input | Provisional active-high control; do not connect directly to TDS VCC. |
| GPIO 5 | Cable detect via GX16 pin 8 | Optional; disabled by default. |
| GPIO 18 | Backlight driver control | Optional; driver and screen wiring must be checked first. |
| 3V3 | ADS1115 VDD, TFT logic supply, DS18B20 VDD | Also the proposed TDS supply only if its module supports 3.3 V. Check the regulator's available current. |
| GND | ADS1115 GND, all TFT GNDs, GX16 pin 7, switch ground | Common reference ground. |
| 5 V / VBUS | GX16 pin 1, for pH module VCC | Only if the pH module requires 5 V; verify the board pin and USB supply capacity. |

GPIO 26-37 are reserved for flash/PSRAM. GPIO 0, 3, 45 and 46 are strapping
pins; GPIO 19 and 20 are reserved here for native USB. GPIO 38 and 48 are
also left unused pending board-revision checks. These are not available for
the proposed connections above.

## Three GC9A01 displays

Connect each display's VCC to a supply supported by that **specific breakout**
and its GND to common ground. The table assumes the display accepts 3.3 V
power and 3.3 V SPI signals. Some boards label SPI MOSI as `SDA`; this is not
the ADS1115's I2C SDA. Each CS must be separate, as shown above. Leave any MISO
pin unconnected unless the display hardware and future firmware require it.

For adjustable brightness, connect GPIO 18 to a **suitable backlight driver**
designed for the actual display backlight pins and current. Follow the display
and driver datasheets. Do not connect three backlight loads directly to one
GPIO. If a breakout has its backlight permanently wired on, leave GPIO 18
unconnected and expect no software brightness control. Do not assume its BL
pin can be tied to a supply without checking the module.

## ADS1115 ADC

| ADS1115 pin | Connect to |
| --- | --- |
| VDD / VCC | ESP32 3V3 |
| GND | Common ground |
| SDA | GPIO 8 |
| SCL | GPIO 9 |
| ADDR | GND for address `0x48` |
| A0 | pH module output through a verified divider, if needed |
| A1 | TDS module analog output through suitable protection, if needed |
| A2, A3 | Leave unconnected for now |
| ALERT / RDY | Leave unconnected for now |

SDA and SCL require pull-ups to **3.3 V**, typically 4.7 kohm each. Check
whether the breakout already has suitable pull-ups before adding them; avoid
pull-ups to 5 V or unnecessarily low combined resistance. The firmware's ADC
adapter uses `0x48` by default, so ADDR must match.

The ADC gain setting does not protect its inputs. Keep each analog input within
the normal supply rails, including startup, shutdown and transients. The
datasheet's VDD + 0.3 V limit is an absolute maximum, not a target voltage.
Do not connect a module with an unknown output range until it is measured and
the appropriate input circuit has been designed.

### pH board and A0 filter

The pH board has pins `To`, `Do`, `Po`, `G`, `G` and `U+`:

| pH board pin | Connect to |
| --- | --- |
| `U+` | Verified 5 V via GX16 pin 1 |
| `G` | Common ground via GX16 pin 7; verify the two `G` pins are ground on the actual board |
| `Po` | GX16 pin 4, then the divider and ADS1115 A0 below |
| `To`, `Do` | Leave unconnected |

The analog output `Po` decreases as pH increases. Use a negative calibration
slope; do not assume the board has a theoretical probe-only gain.

For a board confirmed to output 0-5 V, the **provisional** divider and filter
in the main enclosure are:

```text
pH Po ---- GX16 pin 4 ---- 12 kohm ----+---- ADS1115 A0
                                        |
                                +-------+-------+
                                |               |
                             18 kohm          100 nF
                                |               |
                                +-------+-------+
                                        |
                                   common GND
```

The nominal ADC voltage is module output multiplied by 0.6: 5.0 V becomes
3.0 V. Place the 100 nF capacitor between A0 and ground, close to the ADS1115.
The capacitor filters noise; it is not overvoltage or back-power protection.
Verify the actual maximum voltage, resistor tolerances, source loading, ADC
settling and protection during startup and power loss before applying power.
The calibration code reconstructs `Po` voltage using the nominal ratio.

With the BNC input shorted, the onboard offset trimmer is expected to allow
about 2.5 V at `Po`; confirm this against the exact board and its instructions.
This electrical offset check is not a pH 7 buffer calibration. Do not short the
BNC with a probe connected; use real buffer solutions for calibration. Changing
the trimmer or replacing the board changes the measurement chain and can make a
previous probe-health baseline inappropriate.

The initial software voltage checks assume a 3.3 V ADC supply, a 0.6 divider,
a 0-5 V board output and a 0.05 V margin near the ADC and board rails. These are
provisional values, not measured characteristics; review them in
[include/config/ph.h](../include/config/ph.h) for the actual board. Out-of-range
and railed readings are classified as faults, not pH values. A plausible analog
voltage does not prove a probe is attached. These checks are not yet connected
to the physical sensor task.

Check the TDS output independently. If its supply is switched off, check that
its analog output cannot back-power the module through A1 or exceed the
unpowered module's limits. Add appropriate isolation or protection according to
the module datasheet; do not assume a voltage divider alone resolves this.

## Probe-enclosure cable: GX16, 8 pins

The ADS1115 is assumed to be in the main enclosure. The pH and TDS modules are
assumed to be in the probe enclosure, close to their probes. **Verify actual
connector numbering on both mating halves with a continuity meter**; do not
rely on a drawing's front/back orientation.

| GX16 contact | Main enclosure | Probe enclosure |
| --- | --- | --- |
| 1 | Verified +5 V supply | pH board `U+` |
| 2 | 3.3 V | DS18B20 VDD |
| 3 | Output of the TDS high-side supply switch | TDS module VCC |
| 4 | pH divider and 100 nF filter, then ADS1115 A0 | pH board `Po` |
| 5 | TDS signal conditioning, then ADS1115 A1 | TDS module analog output |
| 6 | GPIO 10 and OneWire pull-up | DS18B20 data |
| 7 | Common GND | pH board `G`, TDS module GND and DS18B20 GND |
| 8 | GPIO 5 with pull-up, if cable detect is enabled | Tie to common GND |

Use a shielded cable; connect its shield to ground at the **main enclosure end
only**. The cable-detect wire indicates that the enclosure cable is connected;
it does not prove that the individual analog probes are connected. If not
used, leave it disconnected and keep cable detection disabled.

### DS18B20

Use the externally powered, three-wire configuration: VDD to GX16 pin 2, data
to pin 6, and GND to pin 7. Add a provisional **4.7 kohm resistor from GPIO 10
data to 3.3 V** in the main enclosure. Long cables may need a different pull-up
or wiring treatment after testing. Probe wire colors vary: identify VDD, data
and GND from the probe documentation, not color alone. Parasite-power mode is
not assumed.

## TDS high-side power switch

Switch the TDS module's **VCC**, not its ground. GX16 pin 3 always carries the
switched supply; GX16 pin 7 remains common ground. Prefer a 3.3 V switched
rail only if the specific module accepts it. If it needs 5 V, separately check
its analog output and provide appropriate ADC protection.

One possible 3.3 V high-side circuit, subject to component datasheets and
electrical review:

```text
3.3 V ---- P-MOSFET source
             P-MOSFET drain ---- GX16 pin 3 ---- TDS VCC
             P-MOSFET gate -----+---- N-MOSFET drain
                                |
                             100 kohm
                                |
                               3.3 V

GPIO 4 ---- 1 kohm ----+---- N-MOSFET gate
                       |
                    100 kohm
                       |
                      GND
N-MOSFET source ---------------- GND
TDS GND ---- GX16 pin 7 -------- GND
```

The N-MOSFET must turn on reliably at 3.3 V gate drive; select the P-MOSFET
for the rail, current, inrush and gate-voltage limits. Verify MOSFET pinouts
from their datasheets. The pull-up holds the P-MOSFET off and the pull-down
holds the N-MOSFET off while GPIO 4 is floating at boot. With this particular
circuit, GPIO 4 high enables power. A suitable load-switch IC is an alternative
if its input polarity and default-off behavior are accounted for. A 5 V rail
requires rechecking both MOSFET gate ratings and the module output circuit.

The intended warm-up default is 1 second, but the actual module may need longer.
The current simulation firmware does not control this switch; the switch's
hardware default must remain off independently of firmware. Direct GPIO power
is not approved without checking GPIO current limits, startup inrush and the
module's supply and output requirements.

## Pre-power checklist

1. Confirm the board revision and each module's supply, logic, output voltage,
   connector labels and current requirements against its documentation.
2. With power disconnected, check GX16 contact numbering, common ground,
   absence of shorts between rails, and the DS18B20 pull-up.
3. Confirm ADS1115 I2C pull-ups go to 3.3 V and ADDR is tied to ground.
4. Verify the pH and TDS input protection and ensure powered-off modules cannot
   be back-powered through their signal lines.
5. Verify the TDS switch is off with GPIO 4 unpowered or floating, and check
   backlight drive and power-supply current limits.
6. Only after the circuit has been reviewed, apply power and measure supply and
   signal voltages with a meter before attaching them to the ESP32 or ADC as
   appropriate. Do not expect real readings until hardware mode is implemented.

The authoritative proposed GPIO constants are in
[include/config/pins.h](../include/config/pins.h). Firmware progress and build
instructions are in [README.md](../README.md).