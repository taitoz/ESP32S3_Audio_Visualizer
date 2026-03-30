# ESP32-S3 Audio Visualizer

Multi-feature audio visualizer for **LilyGo T-Display-S3-Long** (touchscreen version) with ESP32-S3.

## Features

### Phase 1 (Current)
- **Spectrum Analyzer** — 32-band FFT from GPIO ADC input via audio transformer
- **VU Meters** — 3 styles: Classic Needle, LED Ladder, Retro Analog
- **Touch switching** — tap screen to cycle between visualization modes
- Peak hold with decay on spectrum bars
- VU ballistics (fast attack, slow release)
- FPS counter

### Phase 2 (Planned)
- AK4493 DAC control via SPI (volume, filter, sound mode, mute)
- DAC settings UI page

### Phase 3 (Planned)
- BLE client for Samsung Gear VR controller
- USB HID mouse/keyboard output
- Controller settings UI

## Hardware

### Board
- LilyGo T-Display-S3-Long (touchscreen version)
- ESP32-S3 with 16MB Flash, 8MB OPI PSRAM
- 640×180 QSPI display (AXS15231B)
- Capacitive touch (I2C, address 0x3B)

### Audio Input Circuit
```
Audio Source → Audio Transformer → 100nF Cap → GPIO3 (ADC1_CH2)
                                                  |
                                            100k ─┤─ 100k
                                                  |     |
                                               3.3V   GND
```
The transformer provides galvanic isolation. The resistor divider biases the ADC input at ~1.65V (mid-range). The coupling capacitor blocks DC.

### AK4493 DAC (Phase 2)
- SPI control on SPI3 (HSPI): SCK=GPIO39, MOSI=GPIO40, MISO=GPIO41, CS=GPIO42

## Build Settings (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | ESP32-S3-Dev |
| USB CDC On Boot | Enabled |
| Flash Size | 16MB |
| Partition Scheme | 16M Flash (3MB APP / 9.9MB FATFS) |
| PSRAM | OPI PSRAM |

## Dependencies

- **TFT_eSPI** 2.5.34+ (sprite-only mode, no TFT_eSPI display driver used directly)
- **arduinoFFT** (for FFT processing)
- **ESP32 Arduino Core** 3.x

## Usage

1. Flash the firmware
2. Connect audio signal via transformer to GPIO3
3. Touch the screen to cycle modes:
   - **SPECTRUM** — 32-band bar analyzer with peak dots
   - **VU NEEDLE** — Classic analog needle meter
   - **VU LED** — Horizontal segmented LED bar (RMS + Peak)
   - **VU RETRO** — Warm retro dual analog meter

## Credits

- Display driver based on [nikthefix's TFT_eSPI support](https://github.com/nikthefix/Lilygo_Support_T_Display_S3_Long_TFT_eSPI_Volos) for LilyGo T-Display-S3-Long
- Calculator GUI concept by Volos Projects
