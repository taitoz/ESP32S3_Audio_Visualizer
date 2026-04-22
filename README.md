# ESP32-S3 Audio Visualizer + DAC Controller + BLE HID Remote

Multi-feature platform for **LilyGo T-Display-S3-Long** (touchscreen version, ESP32-S3).
Combines real-time audio visualization, AKM AK4493 DAC control, and Samsung Gear VR controller BLE-to-USB-HID bridging into a single device.

> See [PROJECT_CONTEXT.md](PROJECT_CONTEXT.md) for full architecture, design decisions, and implementation roadmap.

---

## Project Status

| Phase | Description | Status |
|-------|-------------|--------|
| **1** | Spectrum Analyzer + VU Meters + Touch Switching | **Done** |
| **2** | AK4493 DAC SPI Driver + Settings UI | Planned |
| **3** | BLE Gear VR Controller Client | **Done** |
| **4** | USB HID Mouse + Consumer Control Output | **Done** |
| **5** | Web Serial Settings UI + NVS | **Done** |
| **6** | Dual-core FreeRTOS Task Architecture | **Done** |

---

## Features (Phase 1 — Current)

- **Technics EQ (SH-GE70 style)** — 10-band FFT spectrum analyzer with authentic VFD cyan-to-red gradient, logarithmic frequency mapping, peak hold dots
- **Technics VU (RS-TR373 style)** — Dual horizontal VU meters with 16 amber segments per channel, dB scale (-20 to +6), peak hold with fade
- **Touch Mode Switching** — tap screen to cycle: EQ → VU
- **Auto-Brightness** — ambient light sensor adjusts backlight (10-255) once per second to prevent flickering
- **Dual-Core FreeRTOS** — Core 0: touch + serial + light sensor, Core 1: audio + FFT + display
- **Web Serial UI** — real-time settings control via browser (band smoothing, peak hold, VU attack/release)
- **Performance** — ~10 FPS for both modes, full-frame 640×180 QSPI push with software 90° rotation
- **Samsung Gear VR Controller → USB HID** (Phase 3 + 4) — BLE (NimBLE central) + composite USB HID (Mouse + Consumer Control) bridge. See [Gear VR Controls](#gear-vr-controls) below.

---

## Gear VR Controls

The Gear VR controller connects over BLE (hardcoded MAC) and emulates a USB composite device: **Mouse** + **Consumer Control** (media keys). The pointer is **gyroscope-driven** (angular rate → cursor velocity), not touchpad-delta-driven.

| Gear VR input | Host action |
|---|---|
| **Trigger** (hold) | Air-mouse gate — cursor only moves while held |
| **Touchpad click, L-zone** (x ≤ 160) | LEFT mouse click |
| **Touchpad click, R-zone** (x >  160) | RIGHT mouse click |
| **Touchpad swipe** (finger on pad, ΔY) | Scroll wheel |
| **Touchpad swipe** (finger on pad, ΔX) | Horizontal pan |
| **Home** | MUTE (media key) |
| **Back** | PLAY / PAUSE (media key) |
| **Volume +** | Volume Increment |
| **Volume −** | Volume Decrement |

Touchpad X is 10-bit but the HW only reports `0..~315`, so the L/R zone split sits at **160**. The zone is **latched on the click's rising edge** so a drag mid-hold cannot flip the button. Additional Consumer usage codes (`SCAN_NEXT_TRACK 0xB5`, `SCAN_PREV_TRACK 0xB6`, `AC_BACK 0x224`, `AC_FORWARD 0x225`) are defined but not wired to buttons yet — reserved for future gestures.

See `PROJECT_CONTEXT.md` §8 for the full pointer-ballistics algorithm and regression-guard table.

---

## Hardware

### Board
- **LilyGo T-Display-S3-Long** (touchscreen version)
- **MCU**: ESP32-S3 (dual-core Xtensa LX7, 240 MHz)
- **Flash**: 16 MB
- **PSRAM**: 8 MB OPI
- **Display**: 640x180 QSPI (AXS15231B controller), software 90-degree rotation for landscape
- **Touch**: Capacitive, I2C @ address 0x3B (SDA=GPIO15, SCL=GPIO10, INT=GPIO11, RST=GPIO16)
- **USB**: Native USB-OTG (USB CDC + USB HID capable)

### Audio Input Circuit
```
                         Audio Transformer
Audio Source ───[ ]───┤ Primary  Secondary ├───[ 100nF ]───┬─── GPIO3 (ADC1_CH2)
                      └────────────────────┘               │
                                                     100k ─┤─ 100k
                                                           │     │
                                                        3.3V    GND
```
- **Transformer**: provides galvanic isolation from source
- **100nF coupling cap**: blocks any DC offset from transformer
- **2x 100k resistors**: bias network sets DC midpoint at ~1.65V (center of ADC range)
- **ADC**: 12-bit, 0–3.3V range (ADC_ATTEN_DB_11), sampled at 22050 Hz

### AK4493 DAC Connection (Phase 2)
```
ESP32-S3           AK4493
────────           ──────
GPIO39 (SCK)  ───→ CCLK
GPIO40 (MOSI) ───→ CDTI (serial data in)
GPIO41 (MISO) ←─── CDTO (serial data out / readback)
GPIO42 (CS)   ───→ CSN  (chip select, active low)
```
- SPI3_HOST (HSPI) at 1 MHz — completely separate bus from display QSPI (SPI2)

### Gear VR Controller (Phase 3 — Done)
- Connects via **BLE** (ESP32-S3 acts as BLE Central/Client via NimBLE)
- Service UUID: `4f63756c-7573-2054-6872-65656d6f7465` ("Oculus Three Remote")
- 60-byte notification packet provides: 10-bit touchpad X/Y, trigger, home, back, touchpad-click, volume ± buttons, 3-axis accelerometer, 3-axis gyroscope, 3-axis magnetometer, battery %
- Auto-reconnect every 15 s, 60-second data-timeout watchdog, keep-alive writes + battery-read pings

---

## Pin Map

Authoritative source: [`ESP32S3_Audio_Visualizer/pins_config.h`](ESP32S3_Audio_Visualizer/pins_config.h). Table below mirrors it.

### Display (QSPI, SPI2_HOST, 32 MHz)

| GPIO | Signal | Notes |
|------|--------|-------|
| 12 | `TFT_QSPI_CS`  | Chip select |
| 17 | `TFT_QSPI_SCK` | QSPI clock |
| 13 | `TFT_QSPI_D0`  | Data 0 |
| 18 | `TFT_QSPI_D1`  | Data 1 |
| 21 | `TFT_QSPI_D2`  | Data 2 — **shared with `PIN_BUTTON_2`** |
| 14 | `TFT_QSPI_D3`  | Data 3 |
| 16 | `TFT_QSPI_RST` | Display reset — **shared with `TOUCH_RES`** |
|  1 | `TFT_BL`       | Backlight (PWM capable) |

### Touch (capacitive, I2C @ 0x3B)

| GPIO | Signal | Notes |
|------|--------|-------|
| 15 | `TOUCH_IICSDA` | I2C SDA (Wire) |
| 10 | `TOUCH_IICSCL` | I2C SCL (Wire) |
| 11 | `TOUCH_INT`    | Active-LOW touch interrupt (INPUT_PULLUP) |
| 16 | `TOUCH_RES`    | Reset — shared with display RST |

### Audio Input (stereo, ADC1)

| GPIO | Signal | ADC channel | Notes |
|------|--------|-------------|-------|
| 3 | `AUDIO_ADC_PIN_L` | ADC1_CH2 | Left — transformer + 100 nF + 2×100 k bias |
| 4 | `AUDIO_ADC_PIN_R` | ADC1_CH3 | Right — transformer + 100 nF + 2×100 k bias |

### Ambient Light Sensor

| GPIO | Signal | ADC channel | Notes |
|------|--------|-------------|-------|
| 5 | `LIGHT_SENSOR_PIN` | ADC1_CH4 | LDR voltage divider / phototransistor → auto-brightness |

### Buttons & Battery

| GPIO | Signal | Notes |
|------|--------|-------|
| 0 | `PIN_BUTTON_1` | BOOT button |
| 21 | `PIN_BUTTON_2` | Shared with display `D2` |
| 8 | `PIN_BAT_VOLT` | Battery voltage via on-board divider |

### AK4493 DAC (SPI3_HOST / HSPI, 1 MHz) — Phase 2

| GPIO | Signal | Notes |
|------|--------|-------|
| 39 | `AK4493_SPI_SCK`  | CCLK |
| 40 | `AK4493_SPI_MOSI` | CDTI |
| 41 | `AK4493_SPI_MISO` | CDTO (register readback) |
| 42 | `AK4493_SPI_CS`   | CSN (active low) |
| 38 | `DAC_RESET_PIN`   | AK4493 hardware reset (active low) |
| 37 | `AMP_MUTE_RELAY_PIN` | MOSFET gate — amplifier power / mute relay |

### RTC DS3231 (I2C1, 400 kHz, addr 0x68)

| GPIO | Signal | Notes |
|------|--------|-------|
| 6 | `RTC_I2C_SDA` | Dedicated bus — not shared with touch |
| 7 | `RTC_I2C_SCL` | |

### USB (native OTG) — Composite CDC + HID

| GPIO | Signal | Notes |
|------|--------|-------|
| 19 | USB D− | CDC Serial + HID Mouse + HID Consumer Control |
| 20 | USB D+ | (same) |

**Pin-sharing notes**:
- `GPIO 16` serves **both** display reset and touch reset — pulses during `setup()` reset both chips simultaneously, which is intentional.
- `GPIO 21` serves **both** display D2 (QSPI) and Button 2 — button reads will only work while QSPI traffic is idle; in this project Button 2 is not polled.
- **ADC1 only** is used for Audio + Light — ADC2 on ESP32-S3 shares the radio subsystem with WiFi (BLE is unaffected, but keeping everything on ADC1 removes any doubt).

---

## Build Settings (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | ESP32-S3-Dev |
| USB CDC On Boot | Enabled |
| USB Mode | USB-OTG CDC(TinyUSB) |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| CPU Frequency | 240MHz |

---

## File Structure

```
ESP32S3_Audio_Visualizer/
├── README.md                              ← this file
├── PROJECT_CONTEXT.md                     ← architecture, decisions, phase roadmap
└── ESP32S3_Audio_Visualizer/
    ├── ESP32S3_Audio_Visualizer.ino       ← main: FreeRTOS tasks, touch, mode switching
    ├── pins_config.h                      ← all pin definitions and hardware constants
    ├── AXS15231B.cpp / .h                 ← QSPI display driver with 90° rotation
    ├── audio_sampling.cpp / .h            ← timer-driven ADC double-buffer (1024 @ 22050Hz)
    ├── spectrum.cpp / .h                  ← FFT processing, 10-band log-scale
    ├── technics_vfd.cpp / .h              ← Technics EQ + VU rendering (SH-GE70, RS-TR373)
    ├── settings.cpp / .h                  ← NVS persistence, Web Serial JSON API
    ├── serial_cmd.cpp / .h                ← Serial command parser for settings
    └── light_sensor.cpp / .h              ← Ambient light sensor for auto-brightness
```

---

## Quick Start

1. Install dependencies via Arduino Library Manager
2. Open `ESP32S3_Audio_Visualizer/ESP32S3_Audio_Visualizer.ino`
3. Select board & build settings as shown above
4. Wire audio transformer output to GPIO3 with bias circuit
5. Flash and run
6. Touch the screen to cycle visualization modes (EQ ↔ VU)
7. Open `settings.html` in Chrome/Edge to configure via Web Serial

## Web Serial UI

The `settings.html` file provides a browser-based configuration interface:

**Features:**
- Real-time parameter adjustment (ADC sensitivity, band smoothing, peak hold, VU attack/release)
- Live FPS and heap monitoring
- No app installation required — works directly in Chrome/Edge
- USB Serial connection @ 115200 baud

**Usage:**
1. Open `settings.html` in Chrome or Edge browser
2. Click "Connect COM Port" and select ESP32-S3 device
3. Adjust parameters in real-time
4. Changes are saved to NVS automatically

**Available Settings:**
- **Visualization**: Mode selection, ADC sensitivity (50-2000)
- **Spectrum**: Band smoothing (0-0.99), peak fall rate (0.1-3.0), peak hold frames (0-60)
- **VU Meter**: Attack speed (0.01-1.0), release speed (0.01-1.0)
- **Display**: Manual brightness (0-255), FPS display

---

## Credits

- **Display driver** based on [nikthefix's TFT_eSPI support](https://github.com/nikthefix/Lilygo_Support_T_Display_S3_Long_TFT_eSPI_Volos) for LilyGo T-Display-S3-Long
- **Calculator GUI concept** by Volos Projects
- **Gear VR BLE protocol** based on community reverse-engineering efforts
