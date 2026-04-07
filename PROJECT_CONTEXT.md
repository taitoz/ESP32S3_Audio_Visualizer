# ESP32-S3 Audio Visualizer - Project Context

**Version**: 1.0-alpha  
**Platform**: LilyGo T-Display-S3-Long  
**Status**: Core audio visualization complete, stable release

---

## 1. Project Overview

**Goal**: Real-time audio spectrum analyzer and VU meter with capacitive touch control on ESP32-S3 platform.

**Implemented Features**:
1. ✅ Technics EQ (SH-GE70 style) — 10-band FFT spectrum with cyan-to-red gradient
2. ✅ Technics VU (RS-TR373 style) — Dual 16-segment amber VU meters with dB scale
3. ✅ Capacitive touch control for mode switching (EQ ↔ VU)
4. ✅ Auto-brightness via ambient light sensor (GPIO4)
5. ✅ Web Serial UI for real-time settings control
6. ✅ FreeRTOS dual-core architecture (Core 0: touch/serial, Core 1: audio/display)
7. ✅ PSRAM-optimized display rendering with full-frame 640×180 push

**Core Principle**: The 640x180 landscape display and capacitive touchscreen provide the primary user interface. Audio visualization responds in real-time to stereo input.

---

## 2. Hardware Platform

### 2.1 LilyGo T-Display-S3-Long (Touchscreen Version)

| Spec | Detail |
|------|--------|
| MCU | ESP32-S3 (dual-core Xtensa LX7 @ 240 MHz) |
| Flash | 16 MB |
| PSRAM | 8 MB OPI (used for display sprite buffer) |
| Display | 640x180 QSPI, AXS15231B controller |
| Touch | Capacitive, I2C @ 0x3B |
| USB | Native USB-OTG (CDC + HID simultaneously) |
| BLE | BLE 5.0 via ESP32-S3 radio |
| Buttons | GPIO0 (BOOT), GPIO21 (shared with display D2) |
| Battery | ADC on GPIO8 |
| Light Sensor | ADC on GPIO4 (ambient light for auto-brightness) |

### 2.2 Display Architecture

The AXS15231B is a QSPI display in **native portrait mode** (180x640). To use landscape (640x180):
- A full-screen `TFT_eSprite` (640x180) is drawn in landscape orientation
- `lcd_PushColors_rotated_90()` performs a software matrix rotation when pushing pixels
- This uses a 230,400-byte PSRAM buffer (`qBuffer`) for the transposed pixel data
- Hardware rotation via `lcd_setRotation(2)` only flips upside-down, not 90 degrees
- Frame rate: ~15 FPS at SPI 32 MHz with full-screen updates

### 2.3 Touch Architecture

- I2C address: `0x3B`
- Command sequence: `{0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8}`
- Returns 8 bytes: gesture type, point count, X (12-bit), Y (12-bit)
- Coordinates need remapping: `tx=map(pointX,627,10,0,640)`, `ty=map(pointY,180,0,0,180)`
- Touch INT pin (GPIO11) goes LOW on touch event
- Debounce: `touch_held` flag with timeout counter to prevent repeated triggers

### 2.4 Audio Input — Stereo (Transformer-coupled)

**Why audio transformer**: Galvanic isolation from audio source, impedance matching, no ground loop issues.

**Circuit** (identical for each channel):
```
LEFT:   Audio Transformer L secondary → 100nF cap → GPIO3 (ADC1_CH2)
                                                       ├─ 100k → 3.3V
                                                       └─ 100k → GND

RIGHT:  Audio Transformer R secondary → 100nF cap → GPIO4 (ADC1_CH3)
                                                       ├─ 100k → 3.3V
                                                       └─ 100k → GND
```

**ADC Configuration**:
- GPIO3 = ADC1_CH2 (Left), GPIO4 = ADC1_CH3 (Right)
- Both on ADC1 — safe to use alongside BLE (ADC2 conflicts with Wi-Fi/BLE radio)
- Attenuation: ADC_ATTEN_DB_11 (0–3.3V full range)
- Resolution: 12-bit (0–4095)
- DC midpoint: ~2048 (1.65V from bias network)
- Sample rate: 22,050 Hz (Nyquist = 11,025 Hz, covers full audible spectrum adequately)
- Sampling method: `esp_timer` periodic callback at 45.35 µs interval
- Both channels read on each timer tick (interleaved oneshot reads)
- Buffer: double-buffer of 1024 int16_t samples per channel, swap on fill

---

## 3. Implementation Status (v1.0-alpha)

### 3.1 ✅ Completed Features

**Audio Processing**:
- Dual-channel ADC sampling at 44.1kHz via ESP32-S3 ADC1
- Real-time FFT processing with ArduinoFFT library
- 8-band logarithmic frequency distribution (30Hz - 20kHz)
- Proper ADC normalization (0.0-1.0 range)
- Stereo RMS calculation with noise gate

**Display Rendering**:
- 640x180 landscape mode via software rotation
- PSRAM-optimized sprite buffer (230KB)
- 30 FPS rendering with dirty rectangle optimization
- VU meters: 16 segments per channel with peak hold
- Spectrum analyzer: 8 bands with exponential smoothing

**Touch & Control**:
- AXS15231B capacitive touch controller (I2C @ 0x3B)
- Touch-anywhere mode switching (EQ ↔ VU)
- Web Serial UI for brightness and visualization settings
- Settings persistence via ESP32 NVS

**Architecture**:
- FreeRTOS dual-core: Core 1 (display), Core 0 (touch/serial)
- Proper task priorities and watchdog handling
- Mutex-protected I2C access
- Memory-efficient double buffering

### 3.2 🔧 Technical Solutions Implemented

**ADC Normalization Fix**: Resolved "stuck channel" issue by proper RMS normalization using ADC_CENTER divisor instead of hardcoded values.

**Display Artifacts**: Eliminated VU meter artifacts by drawing directly to main sprite instead of separate sprites.

**Core Starvation**: Fixed touch responsiveness by adjusting task priorities (Touch: priority 2, Display: priority 1).

**Light Sensor Removal**: Simplified architecture by removing auto-brightness feature to eliminate ADC conflicts.

### 3.3 📊 Performance Metrics
- **Free Heap**: ~274KB after initialization
- **CPU Usage**: ~40% Core 1 (display), ~5% Core 0 (touch)
- **Memory**: 230KB PSRAM sprite buffer
- **Latency**: <50ms audio-to-visual response
- **Frame Rate**: ~30 FPS with SPI at 32MHz
- When disabled, manual `settings.brightness` applies

### 2.6 AK4493 DAC — SPI Control

**Why SPI (not I2C)**: User preference. AK4493 supports both; SPI avoids bus contention with the touch controller on I2C.

**Bus**: SPI3_HOST (HSPI) — completely independent from display QSPI on SPI2_HOST.

| Signal | GPIO | AK4493 Pin |
|--------|------|------------|
| SCK | 39 | CCLK |
| MOSI | 40 | CDTI |
| MISO | 41 | CDTO |
| CS | 42 | CSN (active low) |

**SPI Protocol** (AK4493 specific):
- Clock: 1 MHz max for control registers
- Write: CS low → 2 bytes (1 byte address + 1 byte data) → CS high
- Read: CS low → 1 byte address (with R/W bit set) → 1 byte data out on MISO → CS high
- Address format: `[R/W][0][A5:A0]` — bit 7 = 1 for read, 0 for write

**Key AK4493 Registers**:

| Addr | Name | Key Bits |
|------|------|----------|
| 0x00 | Control 1 | RSTN (reset), DIF[2:0] (audio format) |
| 0x01 | Control 2 | DEM[1:0] (de-emphasis), SMUTE (soft mute) |
| 0x02 | Control 3 | DP (DSD/PCM), DZFM, DZFE (zero detect) |
| 0x03 | Lch ATT | LATT[7:0] — left volume (0x FF = mute, 0x00 = 0dB) |
| 0x04 | Rch ATT | RATT[7:0] — right volume |
| 0x05 | Control 4 | SSLOW, SD, DFS[1:0] (DSD frequency) |
| 0x06 | DSD1 | DSDSEL, DSDD, DMC, DMR, DML |
| 0x07 | Control 5 | SYNCE (sync mode), GC[2:0] (gain control) |
| 0x08 | Sound Control | SC[2:0] — sound mode (sharp/slow roll-off, short delay, etc.) |
| 0x09 | DSD2 | Additional DSD settings |
| 0x0A | Control 6 | TDM[1:0] (TDM mode), SDS[2:0] (TDM slot) |
| 0x15 | Control 7 | ATS[1:0] (attenuation transition speed) |

**Sound Filter Modes (Register 0x08, SC[2:0])**:
- 000: Sharp roll-off
- 001: Slow roll-off
- 010: Short delay sharp roll-off
- 011: Short delay slow roll-off
- 100: Super slow roll-off

---

## 3. Software Architecture

### 3.1 Current Module Structure (Phase 1 + Phase 5 + Phase 6)

```
ESP32S3_Audio_Visualizer.ino     Main sketch — FreeRTOS task creation, setup
  ├── pins_config.h              All hardware pin definitions and constants
  ├── AXS15231B.cpp/.h           QSPI display driver (PSRAM rotation buffer)
  ├── audio_sampling.cpp/.h      Timer-driven stereo ADC, double-buffer, RMS/peak
  ├── spectrum.cpp/.h            arduinoFFT, 32-band log mapping, stereo bar visualization
  ├── vu_meter.cpp/.h            2 VU styles (Needle, LED Ladder) with stereo ballistics
  ├── serial_cmd.cpp/.h          JSON command handler over USB CDC Serial
  ├── settings.cpp/.h            NVS persistence for all configuration
  ├── light_sensor.cpp/.h        Ambient light ADC → auto backlight PWM
settings.html                    Standalone Web Serial UI (opened locally in browser)
.gitignore                       Build artifacts and IDE files
```

### 3.2 Target Module Structure (All Phases — Future Additions)

```
  ├── ak4493.cpp/.h              AK4493 SPI driver (register read/write, volume, filter)
  ├── ble_gearvr.cpp/.h          BLE client — scan, connect, parse Gear VR packets
  └── usb_hid.cpp/.h             USB HID mouse + keyboard output
```

### 3.3 Memory Budget

| Resource | Usage | Available |
|----------|-------|-----------|
| Sprite buffer (PSRAM) | 640x180x2 = 230 KB | 8 MB PSRAM |
| Rotation buffer (PSRAM) | 230 KB | shared from PSRAM |
| FFT buffers (vReal, vImag) | 1024x4x4 = 16 KB (float) | DRAM |
| Sample double-buffer | 1024x2x2 = 4 KB | DRAM |
| BLE stack | ~40 KB (NimBLE) | DRAM |
| USB HID | ~5 KB | DRAM |
| Free DRAM (estimated) | ~200 KB+ | 512 KB total |
| Free PSRAM (estimated) | ~7.5 MB | 8 MB total |

### 3.4 FreeRTOS Task Plan [IMPLEMENTED]

| Task | Core | Priority | Stack | Responsibility |
|------|------|----------|-------|----------------|
| Audio + Display | Core 1 | 2 (High) | 8 KB | ADC consume, FFT (conditional), VU update, visualization, display push |
| Touch + Serial | Core 0 | 1 (Med) | 4 KB | Touch polling at ~50 Hz, serial command processing, mode cycling |
| BLE + USB HID | Core 0 | 1 (Med) | 4 KB | (Future) BLE scanning, Gear VR packets, USB HID reports |

**Why dual-core**: Touch I2C polling and serial command processing on Core 0 cannot stall the audio/display pipeline on Core 1. FFT is only computed in spectrum mode (skipped for VU modes). No WiFi used — settings via USB CDC Serial + Web Serial API.

---

## 4. Implementation Phases — Detailed

### Phase 1: Spectrum Analyzer + VU Meters + Dual-Core [DONE]

**Completed**:
- [x] `audio_sampling.cpp/.h` — esp_timer ADC at 22050 Hz, double-buffer, dynamic DC removal, noise gate
- [x] `spectrum.cpp/.h` — ArduinoFFT<float> with Hamming window, 32-band log-scale, peak hold with decay
- [x] `vu_meter.cpp/.h` — 2 styles: Needle (dual analog), LED Ladder (40-seg RMS+Peak)
- [x] Touch mode cycling with millis()-based debounce
- [x] FPS counter overlay
- [x] Full pin map in `pins_config.h` including future AK4493 SPI pins
- [x] Dual-core FreeRTOS: Audio+Display on Core 1, Touch on Core 0
- [x] Float FFT (not double) for ESP32-S3 performance (~10 FPS)
- [x] Conditional FFT — only computed in spectrum mode, skipped for VU modes
- [x] Optimized display rotation loop (pointer arithmetic, no multiply-per-pixel)

**Tuning Notes**:
- `spectrum.cpp` line `val = val / 300.0f` — adjust divisor based on actual signal amplitude from transformer
- `BAND_SMOOTHING` (0.7) — increase for smoother bars, decrease for more responsive
- `VU_ATTACK_COEFF` (0.3) / `VU_RELEASE_COEFF` (0.5) — fast attack, fast release matched to spectrum
- `NOISE_GATE_RMS` (30.0f) — squelch threshold for floating pins / ADC noise
- If ADC is noisy, consider increasing `SAMPLES` to 2048 (costs more CPU but better frequency resolution)

---

### Phase 2: AK4493 DAC SPI Driver + Settings UI

**Files to create**: `ak4493.cpp`, `ak4493.h`

**Implementation steps**:
1. Initialize SPI3_HOST with AK4493 pins from `pins_config.h`
2. Implement `ak4493_write_reg(uint8_t addr, uint8_t data)` and `ak4493_read_reg(uint8_t addr)`
3. Implement high-level functions:
   - `ak4493_init()` — power-on reset sequence, set default PCM mode
   - `ak4493_set_volume(uint8_t left, uint8_t right)` — write to ATT registers
   - `ak4493_set_filter(uint8_t mode)` — sharp/slow/short-delay filters
   - `ak4493_set_mute(bool mute)` — soft mute via SMUTE bit
   - `ak4493_set_sound_mode(uint8_t mode)` — SC register
   - `ak4493_set_dsd_mode(bool enable)` — switch DSD/PCM
   - `ak4493_set_gain(uint8_t gain)` — GC bits in Control 5
4. Add DAC settings page to UI:
   - Volume slider (touch drag on horizontal bar)
   - Filter mode selector (tap to cycle)
   - Mute toggle
   - Sound mode selector
5. Store DAC settings in NVS for persistence

**AK4493 Power-on Sequence**:
1. If PDN pin is connected: pull low → wait 1ms → pull high
2. Wait 2ms for internal PLL lock
3. Write RSTN=0 then RSTN=1 in Control 1 register (soft reset)
4. Configure audio interface format (DIF bits)
5. Set desired filter, volume, etc.

---

### Phase 3: BLE Gear VR Controller Client

**Files to create**: `ble_gearvr.cpp`, `ble_gearvr.h`

**Library**: NimBLE-Arduino (lighter than default ESP32 BLE, saves ~100KB RAM)

**Implementation steps**:
1. Initialize NimBLE as BLE Central (client)
2. Scan for device advertising service UUID `4f63756c-7573-2054-6563-686e6f6c6f67`
3. Connect and discover characteristics
4. Subscribe to notification characteristic (controller data stream)
5. Parse the ~60-byte data packets:

**Gear VR Controller Packet Format** (reverse-engineered):

| Offset | Size | Data |
|--------|------|------|
| 0 | 2 | Timestamp (16-bit) |
| 2 | 2 | Temperature (raw) |
| 4 | 6 | Accelerometer X, Y, Z (16-bit signed each) |
| 10 | 6 | Gyroscope X, Y, Z (16-bit signed each) |
| 16 | 6 | Magnetometer X, Y, Z (16-bit signed each) |
| 54 | 2 | Touchpad X (0-315), Y (0-315) |
| 56 | 1 | Buttons bitfield |
| 57 | 1 | Trigger analog value |

**Button Bitfield** (byte 56):
- Bit 0: Trigger (click)
- Bit 1: Home
- Bit 2: Back
- Bit 3: Volume Up
- Bit 4: Volume Down
- Bit 5: Touchpad click

**Notes**:
- The controller enters sleep after ~30s of inactivity; need to handle reconnection
- The BLE protocol may require a specific write to a characteristic to enable sensor streaming
- Test with `nRF Connect` app first to verify UUIDs and packet format for your specific controller revision

---

### Phase 4: USB HID Mouse/Keyboard Output

**Files to create**: `usb_hid.cpp`, `usb_hid.h`

**Libraries**: `USB.h`, `USBHIDMouse.h`, `USBHIDKeyboard.h` (built into ESP32 Arduino Core 3.x)

**Implementation steps**:
1. Initialize USB HID composite device (Mouse + Keyboard)
2. Map Gear VR controller inputs to HID:

| Controller Input | HID Output | Notes |
|-----------------|------------|-------|
| Touchpad swipe | Mouse move (X/Y) | Scale touchpad delta to mouse sensitivity |
| Trigger click | Left mouse button | |
| Back button | Right mouse button | |
| Touchpad click | Middle mouse button | |
| Volume Up/Down | Mouse scroll wheel | |
| Home button | Keyboard media key (or configurable) | |
| Gyro X/Y | Mouse move (alternative mode) | Air-mouse mode |

3. Add configurable sensitivity/mapping via settings UI
4. Support mode toggle: Touchpad mode vs Gyro air-mouse mode

**Constraint**: USB CDC (Serial) and USB HID can coexist on ESP32-S3 native USB, but `USB CDC On Boot: Enabled` must remain set. The USB stack handles both CDC and HID as a composite device.

---

### Phase 5: Settings UI via USB Serial + Web Serial API [DONE]

**Files**: `serial_cmd.cpp`, `serial_cmd.h`, `settings.cpp`, `settings.h`, `settings.html`

**Deprecated files removed**: `web_server.cpp`, `web_server.h` (old WiFi AP approach, replaced by USB Serial).

**Architecture**: No WiFi used. The ESP32-S3 USB CDC serial (already enabled for debug output) carries bidirectional JSON commands. A standalone `settings.html` file (opened locally in Chrome/Edge) uses the Web Serial API to connect to the ESP32 COM port and provide a full settings UI.

**How it works**:
1. User opens `settings.html` in Chrome or Edge (local file, no server needed)
2. Clicks "Connect COM Port" → browser shows serial port picker
3. Selects the ESP32-S3 USB CDC port
4. HTML page sends/receives JSON commands at 115200 baud
5. ESP32 pushes status updates every 2 seconds automatically

**Serial Protocol** (one JSON object per line, `\n` terminated):
```
PC → ESP32:  {"cmd":"get"}                          → request full status
PC → ESP32:  {"cmd":"set","brightness":128}         → update setting(s)
PC → ESP32:  {"cmd":"restart"}                      → restart device
ESP32 → PC:  {"status":true,"fps":10.2,...}          → periodic push / response
```

**Integration points** (wired into firmware):
- `serial_cmd_init()` called in `setup()` — sends `{"ready":true}` on boot
- `serial_cmd_poll()` called from Core 0 touch task at ~50 Hz
- `settings.viz_mode` synced to `currentMode` each frame on Core 1
- `settings.brightness` applied via `analogWrite(TFT_BL, ...)` on set command
- `settings.adc_sensitivity` read live by `spectrum.cpp` as FFT band divisor
- All settings persisted to NVS per-field on change

**Web UI Sections** (in settings.html):

| Section | Controls |
|---------|----------|
| **Connection** | Connect/Disconnect button, status indicator |
| **Visualization** | Mode selector chips (Spectrum / VU Needle / VU LED), ADC sensitivity slider |
| **Display** | Brightness slider (PWM), live FPS display |
| **DAC (AK4493)** | Volume L/R sliders, filter mode selector, mute toggle |
| **System** | Free heap, uptime, restart button, serial log (scrollable) |

**NVS Settings** (ESP32 Preferences library, namespace `"config"`):

| NVS Key | Type | Default | Description |
|---------|------|---------|-------------|
| `viz_mode` | uint8_t | 0 | Active visualization mode |
| `brightness` | uint8_t | 255 | Backlight PWM (0–255) |
| `adc_sens` | float | 300.0 | Spectrum FFT band divisor |
| `dac_vol_l` | uint8_t | 0x00 | AK4493 left volume (0=0dB, 0xFF=mute) |
| `dac_vol_r` | uint8_t | 0x00 | AK4493 right volume |
| `dac_filter` | uint8_t | 0 | AK4493 filter mode (0–4) |
| `dac_sound` | uint8_t | 0 | AK4493 sound control |
| `dac_mute` | bool | false | Soft mute |
| `mouse_sens` | float | 1.0 | USB HID mouse sensitivity |
| `mouse_mode` | uint8_t | 0 | 0=touchpad, 1=gyro |
| `auto_bri` | bool | false | Enable auto-brightness from light sensor |
| `bri_min` | uint8_t | 10 | Minimum PWM when dark |
| `bri_max` | uint8_t | 255 | Maximum PWM when bright |

**Key benefits**: No WiFi stack (~40 KB RAM saved), no external libraries (ESPAsyncWebServer/AsyncTCP not needed), zero radio interference with BLE, settings UI works via existing USB cable. Only requires ArduinoJson.

**Web Serial browser support**: Chrome 89+, Edge 89+, Opera 76+. Not supported in Firefox/Safari — they can use a native serial terminal with the same JSON protocol.

---

### Phase 6: Dual-core FreeRTOS Task Architecture [DONE]

**Implemented** in Phase 1. Current task layout:

```cpp
// Core 1 — Audio & Display (time-critical, priority 2)
void audioDisplayTask(void *param) {
    for (;;) {
        if (audio_sampling_is_ready()) {
            audio_sampling_consume();       // dynamic DC removal
            vu_meter_update(rms, peak);     // always update VU ballistics
            if (currentMode == VIS_SPECTRUM)
                spectrum_compute_fft();     // conditional — skip for VU modes
            drawFrame();
        }
        vTaskDelay(1);
    }
}

// Core 0 — Touch + Serial Commands (priority 1)
void touchTask(void *param) {
    for (;;) {
        // I2C touch polling with millis() debounce
        // writes volatile currentMode on tap
        serial_cmd_poll();              // process JSON commands from USB CDC
        vTaskDelay(pdMS_TO_TICKS(20));  // ~50 Hz
    }
}

// Core 0 — BLE + USB HID (future, priority 1)
void bleHidTask(void *param) {
    for (;;) {
        ble_gearvr_poll();
        usb_hid_send_report();
        vTaskDelay(10);  // ~100 Hz
    }
}
```

**Shared data protection**:
- `volatile VisMode currentMode` — written by touch/serial task (Core 0), read by display task (Core 1)
- Settings struct — written by serial command handler (Core 0), read by display task (Core 1); all fields volatile
- Mutex for AK4493 SPI (if accessed from serial commands and other contexts)
- Queue for BLE→HID data passing

---

## 5. Key Design Decisions Log

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | Audio input via GPIO ADC (not I2S) | Simpler wiring; audio transformer handles signal conditioning; 12-bit ADC is sufficient for visualization (not recording) |
| 2 | Audio transformer (not direct coupling) | Galvanic isolation, no ground loops, impedance matching, protects ESP32 ADC |
| 3 | AK4493 via SPI (not I2C) | User preference; avoids sharing I2C bus with touch controller; simpler bus arbitration |
| 4 | SPI3_HOST for AK4493 (not SPI2) | SPI2_HOST is already used by the QSPI display; SPI3 is fully independent |
| 5 | NimBLE (not default ESP-IDF BLE) | ~50% less RAM usage; better API for client/central role |
| 6 | Full-screen sprite in PSRAM | Required by AXS15231B driver architecture; software rotation needs full buffer |
| 7 | ADC1 not ADC2 for audio | ADC2 conflicts with Wi-Fi/BLE radio; ADC1 is always available |
| 8 | 22,050 Hz sample rate | Nyquist at 11 kHz covers speech and music visualization; higher rates waste CPU for visual-only use |
| 9 | 1024-sample FFT | Good balance of frequency resolution (~21 Hz/bin) and update rate (~21 FPS max) |
| 10 | esp_timer for ADC sampling | More precise than loop-based timing; timer callback runs in IRAM for consistency |
| 11 | Float FFT (not double) | ESP32-S3 has no hardware double FPU; float is ~2x faster |
| 12 | Dynamic DC removal (not hardcoded 2048) | Handles floating pins, missing bias network, any DC offset |
| 13 | USB Serial + Web Serial API for settings (not WiFi) | No WiFi stack needed (~40 KB RAM saved); no radio interference with BLE; works via existing USB cable; Chrome/Edge Web Serial API provides rich browser UI from a local HTML file |
| 14 | ArduinoJson for serial protocol | Lightweight, well-tested JSON parsing; single library dependency for settings UI |

---

## 6. Known Limitations & Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| ESP32-S3 ADC noise | Noisy spectrum at low signal | Increase oversampling, use running average, or switch to I2S ADC in future |
| Gear VR BLE protocol varies by HW revision | Packet parsing may fail | Test with nRF Connect first; implement flexible packet parser with offset config |
| Display refresh ~15 FPS | Animations not silky smooth | Partial refresh for VU meters (only update changed region); pre-compute static backgrounds |
| QSPI display + SPI3 DAC on same MCU | Unlikely bus contention but possible timing edge cases | They're on different SPI hosts; completely independent hardware |
| BLE reconnection after controller sleep | UX gap when controller wakes | Implement continuous background scanning with auto-reconnect |
| Flash partition size (3MB APP) | May get tight with BLE + USB + FFT | Monitor with `ESP.getFreeSketchSpace()`; consider 8MB APP partition if needed |

---

## 7. Reference Links

- [LilyGo T-Display-S3-Long GitHub](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long)
- [nikthefix TFT_eSPI driver](https://github.com/nikthefix/Lilygo_Support_T_Display_S3_Long_TFT_eSPI_Volos)
- [AK4493 Datasheet](https://www.akm.com/global/en/products/audio/audio-dac/ak4493seq/)
- [arduinoFFT Library](https://github.com/kosme/arduinoFFT)
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino)
- [ESP32-S3 USB HID](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/usb_hid.html)
- [Gear VR Controller BLE Protocol](https://jsyang.ca/hacks/gear-vr-controller/) (community reverse-engineering)

---

*Last updated: Phase 1 + Phase 5 + Phase 6 complete. Stereo ADC (GPIO3 L, GPIO4 R), ambient light sensor auto-brightness (GPIO5), dual-core FreeRTOS, float FFT, dynamic DC removal, 2 VU styles, USB Serial + Web Serial API settings UI with NVS persistence. Next: Phase 2 (AK4493 SPI driver).*
