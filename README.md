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
| **3** | BLE Gear VR Controller Client | Planned |
| **4** | USB HID Mouse/Keyboard Output | Planned |
| **5** | Multi-page Touch UI + NVS Settings | Planned |
| **6** | Dual-core FreeRTOS Task Architecture | Planned |

---

## Features (Phase 1 — Current)

- **Spectrum Analyzer** — 32-band FFT from GPIO ADC input via audio transformer, logarithmic frequency mapping, green-yellow-red gradient bars
- **Peak Hold** — white peak dots per band with configurable hold time and fall rate
- **VU Meter — Needle** — classic dual analog needle with dB scale, red zone, ballistic smoothing
- **VU Meter — LED Ladder** — 40-segment horizontal bar for RMS and Peak with dB readout
- **VU Meter — Retro Analog** — warm palette dual meter with shadow needle, green/red arc zones, skeuomorphic face
- **Touch Mode Switching** — tap anywhere on screen to cycle: Spectrum → Needle → LED → Retro
- **FPS Counter** — displayed on each frame

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

### Gear VR Controller (Phase 3)
- Connects via **BLE** (ESP32-S3 acts as BLE Central/Client)
- Service UUID: `4f63756c-7573-2054-6563-686e6f6c6f67`
- Provides: touchpad X/Y, trigger, back, home, volume buttons, gyro/accelerometer

---

## Pin Map

| GPIO | Function | Bus | Notes |
|------|----------|-----|-------|
| 3 | Audio ADC input | ADC1_CH2 | Via transformer + bias network |
| 12 | Display CS | QSPI (SPI2) | |
| 17 | Display SCK | QSPI (SPI2) | |
| 13 | Display D0 | QSPI (SPI2) | |
| 18 | Display D1 | QSPI (SPI2) | |
| 21 | Display D2 / BTN2 | QSPI (SPI2) | Shared with button 2 |
| 14 | Display D3 | QSPI (SPI2) | |
| 16 | Display RST / Touch RST | — | Shared between display and touch |
| 1 | Backlight (TFT_BL) | PWM capable | |
| 15 | Touch SDA | I2C (Wire) | |
| 10 | Touch SCL | I2C (Wire) | |
| 11 | Touch INT | — | Active LOW on touch |
| 0 | Button 1 (BOOT) | — | |
| 8 | Battery voltage ADC | ADC | |
| 39 | AK4493 SCK | SPI3 (HSPI) | Phase 2 |
| 40 | AK4493 MOSI | SPI3 (HSPI) | Phase 2 |
| 41 | AK4493 MISO | SPI3 (HSPI) | Phase 2 |
| 42 | AK4493 CS | SPI3 (HSPI) | Phase 2 |
| 19/20 | USB D-/D+ | USB-OTG | Native USB for HID (Phase 4) |

---

## Build Settings (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | ESP32-S3-Dev |
| USB CDC On Boot | Enabled |
| USB Mode | Hardware CDC and JTAG |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |
| CPU Frequency | 240MHz |

---

## Dependencies (Arduino Library Manager)

| Library | Version | Purpose |
|---------|---------|---------|
| **TFT_eSPI** | 2.5.34+ | Sprite rendering (sprite-only mode) |
| **arduinoFFT** | 2.x | FFT computation |
| **ESP32 Arduino Core** | 3.x | Platform |
| **NimBLE-Arduino** | — | BLE client (Phase 3, future) |

---

## File Structure

```
ESP32S3_Audio_Visualizer/
├── README.md                              ← this file
├── PROJECT_CONTEXT.md                     ← architecture, decisions, phase roadmap
└── ESP32S3_Audio_Visualizer/
    ├── ESP32S3_Audio_Visualizer.ino       ← main sketch: setup, loop, touch, frame dispatch
    ├── pins_config.h                      ← all pin definitions and hardware constants
    ├── AXS15231B.cpp / .h                 ← QSPI display driver (from nikthefix reference)
    ├── audio_sampling.cpp / .h            ← timer-driven ADC double-buffer (1024 @ 22050Hz)
    ├── spectrum.cpp / .h                  ← FFT processing, 32-band log-scale, bar drawing
    └── vu_meter.cpp / .h                  ← 3 VU styles: Needle, LED Ladder, Retro Analog
```

---

## Quick Start

1. Install dependencies via Arduino Library Manager
2. Open `ESP32S3_Audio_Visualizer/ESP32S3_Audio_Visualizer.ino`
3. Select board & build settings as shown above
4. Wire audio transformer output to GPIO3 with bias circuit
5. Flash and run
6. Touch the screen to cycle visualization modes

---

## Credits

- **Display driver** based on [nikthefix's TFT_eSPI support](https://github.com/nikthefix/Lilygo_Support_T_Display_S3_Long_TFT_eSPI_Volos) for LilyGo T-Display-S3-Long
- **Calculator GUI concept** by Volos Projects
- **Gear VR BLE protocol** based on community reverse-engineering efforts
