# Custom firmware (no0406 fork)

This is a personal fork of [justcallmekoko/ESP32DualBandWardriver](https://github.com/justcallmekoko/ESP32DualBandWardriver).
All credit for the original firmware goes to Just Call Me Koko. The original MIT license and copyright
notice in [LICENSE](LICENSE) still apply, and the changes here are released under the same license.

Upstream is not modified. The custom work lives on two branches, each built on top of upstream `main`:

| Branch | Hardware | Role | What it adds |
|---|---|---|---|
| [`jcmk-c5-custom`](https://github.com/no0406/ESP32DualBandWardriver/tree/jcmk-c5-custom) | JCMK C5 Wardriver (Waveshare ESP32-C5 on the JCMK C5 carrier) | SOLO (Core via settings) | Boot menu, charge-only mode, SOLO scan tuning |
| [`xiao-c5-node`](https://github.com/no0406/ESP32DualBandWardriver/tree/xiao-c5-node) | Seeed Studio XIAO ESP32-C5 | NODE | XIAO pin map, status LED, node WiFi forwarding fix |

`main` on this fork is kept as a plain mirror of upstream `main`, so it can be synced without conflicts.

Both branches are tested on hardware: the JCMK C5 as SOLO, and 2× XIAO ESP32-C5 as nodes feeding a JCMK C5 Core.

---

## 1. JCMK C5 Wardriver (`jcmk-c5-custom`)

### Boot menu

Stock firmware always tries the saved WiFi at boot, then falls back to a 60 s access point before it
starts wardriving. This branch replaces that with a menu shown on every boot:

```
== Boot Menu :) ==            15s
> 1. Start WDR now
  2. Join saved WiFi
  3. Start AP (60s)
  4. Charge only

UP/DN:Move SEL:Choose
```

- **Countdown:** 15 s, shown top right. When it runs out, option 1 is used.
- **Any button press stops the countdown**, so the menu then waits for you.
- **UP / DOWN** move the selection (it wraps around). **SELECT** picks it.

| Option | Colour | What happens |
|---|---|---|
| 1. Start WDR now | Green | Skips the WiFi/AP admin phase entirely and starts wardriving straight away (scans begin once there is a GPS fix and an SD card). This is the default. |
| 2. Join saved WiFi | Yellow | Joins the WiFi saved in settings and serves the web UI on the device's IP. Stays there until that WiFi is lost, then wardrives. If the join fails, "Failed to connect" stays on screen for 2 s and it goes straight to wardriving. |
| 3. Start AP (60s) | Cyan | Starts the device's own access point for 60 s (SSID and password from settings). A client connecting opens the web UI; when it disconnects, wardriving starts. If no one connects within 60 s, wardriving starts anyway. |
| 4. Charge only | Magenta | See below. Nothing scans. |

**Exit option in the AP phase:** while the AP is up (option 3), the bottom line shows a green
`> Exit and Start WDR now`. Pressing SELECT shuts the AP down and starts wardriving immediately, both
while waiting for a client and while one is connected.

### Charge-only mode (option 4)

For plugging into USB just to charge. WiFi and BLE are never started, so the device does not scan or
log even if the GPS has a fix.

```
Charge only - radios off
BAT 87%
4.12V
Rate +14.2%/hr
SEL:Reboot to menu
```

| Field | Source | Colour |
|---|---|---|
| `BAT %` | MAX17048 fuel gauge | Green above 95%, yellow 21–95%, red 20% and below, white if no gauge |
| Voltage | MAX17048 cell voltage | Green above 4.15 V, yellow 4.15 V and below |
| Rate | MAX17048 charge rate (%/hr) | `+` charging, `-` draining |

- Values refresh every 3 s and are also printed to serial as `Charge: 87% 4.12V`.
- The screen stays on, dimmed to about 25% (`CHARGE_SCREEN_BRIGHTNESS 64` in `configs.h`, range 0–255).
- SELECT reboots back to the boot menu.
- The GPS module is still powered (the firmware can't switch it off).

**Reading it:** the % estimate can be misleading on some USB-A chargers, so go by the voltage and the
rate. Around 4.15–4.20 V with the rate near 0 means the charger has finished. While current is flowing
the voltage reads a little high; unplugged and rested, about 4.1 V or more is full.

### SOLO scan tuning

Only SOLO mode uses these values (`configs.h`). CORE does not scan WiFi itself, and nodes use their own
timing.

| Setting | Stock | This branch (v3) |
|---|---|---|
| `SOLO_SCAN_MIN_DWELL_MS` | 80 | 60 |
| `SOLO_SCAN_MAX_DWELL_MS` | 120 | 80 |
| `SOLO_BONUS_CHANNEL_COUNT` | 8 | 6 |
| `SOLO_BLE_INTERVAL_MS` | 4000 | 4000 |

Earlier steps that were tried along the way, each kept as its own commit:

| Version | Min dwell | Max dwell | Bonus visits | BLE interval |
|---|---|---|---|---|
| v1 | 45 | 75 | 8 | 4000 |
| v2 | 45 | 75 | 6 | 4000 |
| v3 (current) | 60 | 80 | 6 | 4000 |

### Smaller changes

- **Panel colour macros (`display.h`):** the JCMK panel swaps red and blue, so `ST77XX_CYAN` shows
  yellow and `ST77XX_RED` shows blue. `CYAN`, `YELLOW` and `RED` macros pick the value that shows the
  intended colour. Use them for any new coloured UI. Green, white and magenta are unaffected.
- **`BatteryInterface`:** adds `getBatteryVoltage()` (−1 without a MAX17048) and `getChargeRate()`.
- **`WiFiOps::begin()`** takes the boot menu choice (`BOOT_MODE_*` in `configs.h`) instead of a
  `skip_admin` flag.

### Build and flash

arduino-cli with esp32 core 3.3.0:

```
arduino-cli compile --fqbn "esp32:esp32:esp32c5:PartitionScheme=min_spiffs,PSRAM=enabled,FlashSize=8M" ^
  --build-property "compiler.cpp.extra_flags=-DC5_WARDRIVER" --output-dir build/jcmk src
```

USB flash with esptool. This keeps the saved settings (the settings partition is not written):

```
esptool --chip esp32c5 --port COMx --baud 921600 write-flash ^
  0x2000 src.ino.bootloader.bin 0x8000 src.ino.partitions.bin ^
  0xe000 boot_app0.bin 0x10000 src.ino.bin
```

`boot_app0.bin` is in the esp32 core at `tools/partitions/boot_app0.bin`. The stock SD-card update also
works, as long as only one `.bin` is on the SD root.

The role is read from saved settings, so the same build runs as SOLO or CORE; switch it from the
on-device settings menu.

---

## 2. Seeed XIAO ESP32-C5 nodes (`xiao-c5-node`)

Lets a bare XIAO ESP32-C5 act as a NODE for a JCMK C5 Core. The XIAO has no screen, SD, GPS or buttons;
the Core does the logging and GPS tagging.

### Hardware notes

| Item | XIAO ESP32-C5 |
|---|---|
| USB | Native USB-Serial/JTAG on GPIO13 (D−) / GPIO14 (D+), shows up as `303A:1001` |
| Flash / PSRAM | 8 MB / 8 MB |
| Antenna | U.FL only. **Attach the antenna before running.** |
| Status LED | Yellow user LED, GPIO27, active low |
| Power LED | Red charger LED, driven by the charge IC (not a GPIO). Lit on USB without a battery. |
| BOOT button | GPIO28 |
| Battery sense | GPIO6 (`ADC_BAT`), GPIO26 (`ADC_CRL`) |

Stock pins would break the XIAO: GPS UART on 13/14 kills USB, the LED on 28 is the BOOT button, and SPI
SCK on 6 is battery sense. The `XIAO_C5` block in `configs.h` moves everything:

| Function | Stock pin | XIAO pin |
|---|---|---|
| Status LED | 28 (active high) | 27 (active low) |
| GPS TX / RX | 13 / 14 | 11 (D6) / 12 (D7) |
| SPI SCK / MISO / MOSI | 6 / 2 / 7 | 8 (D8) / 9 (D9) / 10 (D10) |
| SD CS | 10 | 2 (MTMS pad) |
| TFT backlight | 27 | 3 (MTDI pad, unconnected) |
| UP / DOWN buttons | 9 / 8 | 0 (D1) / 25 (D2) |

Nothing is fitted on those pins; they're just moved somewhere harmless. The buttons use pull-downs, so
nothing floats.

### Status LED

- **Solid yellow:** joined the Core and scanning.
- **Off:** waiting for the Core.
- The red charger LED doubles as a power light. For a power light that is always reliable, add an LED
  with about 1 kΩ between 3V3 and GND.

### Firmware changes

- **Node WiFi forwarding fix (all node boards, not only XIAO).** Stock nodes delivered BLE to the Core
  but almost no WiFi. WiFi results were sent as a burst of async ESP-NOW frames, and the next channel
  change and scan started before they left the radio, so they were dropped. BLE got through because
  those results go out one at a time. Each send now waits (up to 20 ms) for the ESP-NOW send callback.
- **LED macros:** `LED_ON` / `LED_OFF` replace hard-coded `HIGH` / `LOW`, so active-low LEDs work.
- **Serial wait capped at 3 s on the XIAO.** Native USB only reports "connected" once a host opens the
  port, so the stock `while (!Serial)` would hang forever on a power bank.
- **Compile-time role:** with `#define NODE`, the run mode is forced to NODE and the admin AP wait is
  skipped, so a node starts on its own. `#define CORE` forces CORE.
- **VS Code tasks** (`.vscode/tasks.json`): Build / Upload / Monitor for NODE and NODE (XIAO).

### Build and flash

```
arduino-cli compile ^
  --fqbn "esp32:esp32:esp32c5:PartitionScheme=min_spiffs,PSRAM=enabled,FlashSize=8M,CDCOnBoot=cdc" ^
  --build-property "compiler.cpp.extra_flags=-DC5_WARDRIVER -DXIAO_C5" ^
  --output-dir build/node_xiao src
```

Or run the **Build + Upload + Monitor (XIAO)** task in VS Code. Flash one board at a time and check each
one on the Core (node count goes up, WiFi count rises) before doing the next.

### How nodes and the Core work together

- Node scan timing is fixed in the node firmware. The Core only hands out channel ranges; it does not
  push dwell, bonus or BLE settings.
- The Core splits the 40 channels (2.4 GHz 1–14 and 5 GHz 36–177) into equal contiguous blocks, one per
  node. Up to 24 nodes.
- The Core removes duplicates with a rolling list of the last 200 MACs, shared across all nodes, and only
  counts results while it has a GPS fix. Parked, the count levels off because repeats are rejected; more
  nodes help when moving, since channels get revisited faster.
- ESP-NOW encryption is off.

Node counts that divide the 40 channels evenly:

| Nodes | Channels each | Rough pass time |
|---|---|---|
| 2 | 20 | ~2.4 s |
| 4 | 10 | ~1.6 s |
| 5 | 8 | ~1.4 s |
| 8 | 5 | ~1.2 s |
| 10 | 4 | ~1.1 s |
| 20 | 2 | ~1.0 s |

Uneven counts don't help: 12 nodes are no faster than 10. BLE is not split between nodes, so the Core's
load grows with every node.

---

## Keeping up with upstream

Remotes: `upstream` = justcallmekoko, `origin` = this fork.

```
git fetch upstream
git checkout main
git merge --ff-only upstream/main
git push origin main

git checkout jcmk-c5-custom
git merge main          # fix any conflicts, rebuild, test on hardware
git push origin jcmk-c5-custom

git checkout xiao-c5-node
git merge main
git push origin xiao-c5-node
```

Merging (not rebasing) keeps the pushed history intact, so no force-push is needed. The GitHub
**Sync fork** button on `main` does the first step too.

Files most likely to conflict when upstream changes: `src/configs.h`, `src/src.ino` and
`WiFiOps::begin()` in `src/WiFiOps.cpp`.
