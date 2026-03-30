# Project Context & Implementation Roadmap

This document captures architecture decisions, hardware details, and the full implementation plan for all project phases. It serves as persistent context for development sessions.

---

## 1. Project Overview

**Goal**: Build a multi-function device on the LilyGo T-Display-S3-Long that combines:
1. Real-time audio spectrum analyzer and VU meters (GPIO ADC via audio transformer)
2. AKM AK4493 DAC control interface (SPI)
3. Samsung Gear VR controller BLE-to-USB-HID bridge (BLE client + USB HID device)
4. Touch-driven multi-page UI for all features

**Core Principle**: The 640x180 landscape display and capacitive touchscreen are the primary user interface. All features are accessed through touch navigation.

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

### 2.4 Audio Input (Transformer-coupled)

**Why audio transformer**: Galvanic isolation from audio source, impedance matching, no ground loop issues.

**Circuit**:
```
Source → Transformer primary | secondary → 100nF coupling cap → GPIO3
                                                                  |
                                                            100k ─┤─ 100k
                                                                  |     |
                                                               3.3V    GND
```

**ADC Configuration**:
- GPIO3 = ADC1_CH2 (safe to use alongside BLE — ADC2 conflicts with Wi-Fi/BLE, ADC1 does not)
- Attenuation: ADC_ATTEN_DB_11 (0–3.3V full range)
- Resolution: 12-bit (0–4095)
- DC midpoint: ~2048 (1.65V from bias network)
- Sample rate: 22,050 Hz (Nyquist = 11,025 Hz, covers full audible spectrum adequately)
- Sampling method: `esp_timer` periodic callback at 45.35 us interval
- Buffer: double-buffer of 1024 int16_t samples each, swap on fill

### 2.5 AK4493 DAC — SPI Control

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

### 3.1 Current Module Structure (Phase 1)

```
ESP32S3_Audio_Visualizer.ino     Main sketch — setup(), loop(), touch handling, frame dispatch
  ├── pins_config.h              All hardware pin definitions and constants
  ├── AXS15231B.cpp/.h           QSPI display driver (PSRAM rotation buffer)
  ├── audio_sampling.cpp/.h      Timer-driven ADC, double-buffer, RMS/peak calculation
  ├── spectrum.cpp/.h            arduinoFFT, 32-band log mapping, bar visualization
  └── vu_meter.cpp/.h            3 VU styles with ballistic smoothing
```

### 3.2 Target Module Structure (All Phases)

```
ESP32S3_Audio_Visualizer.ino     Main sketch — FreeRTOS task creation, setup
  ├── pins_config.h              Hardware pin definitions
  ├── AXS15231B.cpp/.h           Display driver
  ├── audio_sampling.cpp/.h      ADC double-buffer
  ├── spectrum.cpp/.h            FFT + spectrum bars
  ├── vu_meter.cpp/.h            VU meter styles
  ├── ak4493.cpp/.h              AK4493 SPI driver (register read/write, volume, filter)
  ├── ble_gearvr.cpp/.h          BLE client — scan, connect, parse Gear VR packets
  ├── usb_hid.cpp/.h             USB HID mouse + keyboard output
  ├── ui_manager.cpp/.h          Multi-page UI, swipe navigation, touch routing
  └── settings.cpp/.h            NVS persistence for all configuration
```

### 3.3 Memory Budget

| Resource | Usage | Available |
|----------|-------|-----------|
| Sprite buffer (PSRAM) | 640x180x2 = 230 KB | 8 MB PSRAM |
| Rotation buffer (PSRAM) | 230 KB | shared from PSRAM |
| FFT buffers (vReal, vImag) | 1024x8x2 = 16 KB | DRAM |
| Sample double-buffer | 1024x2x2 = 4 KB | DRAM |
| BLE stack | ~40 KB (NimBLE) | DRAM |
| USB HID | ~5 KB | DRAM |
| Free DRAM (estimated) | ~200 KB+ | 512 KB total |
| Free PSRAM (estimated) | ~7.5 MB | 8 MB total |

### 3.4 FreeRTOS Task Plan (Phase 6)

| Task | Core | Priority | Responsibility |
|------|------|----------|----------------|
| Audio + Display | Core 1 | High | ADC sampling (via timer ISR), FFT, visualization, display push |
| BLE + USB HID | Core 0 | Medium | BLE scanning/connection, Gear VR packet parsing, USB HID reports |
| Touch UI | Core 0 | Low | Touch polling, page navigation, settings changes |

**Why dual-core**: BLE radio operations can cause timing jitter. Keeping audio/display on Core 1 and BLE on Core 0 prevents BLE from disrupting the sampling timer or display refresh.

---

## 4. Implementation Phases — Detailed

### Phase 1: Spectrum Analyzer + VU Meters [DONE]

**Completed**:
- [x] `audio_sampling.cpp/.h` — esp_timer ADC at 22050 Hz, double-buffer, RMS/peak
- [x] `spectrum.cpp/.h` — arduinoFFT with Hamming window, 32-band log-scale, peak hold with decay
- [x] `vu_meter.cpp/.h` — 3 styles: Needle (dual analog), LED Ladder (40-seg RMS+Peak), Retro Analog (warm palette)
- [x] Touch mode cycling in main .ino
- [x] FPS counter overlay
- [x] Full pin map in `pins_config.h` including future AK4493 SPI pins

**Tuning Notes** (for when testing on hardware):
- `spectrum.cpp` line `val = val / 300.0f` — adjust this divisor based on actual signal amplitude from transformer
- `BAND_SMOOTHING` (0.7) — increase for smoother bars, decrease for more responsive
- `VU_ATTACK_COEFF` (0.3) / `VU_RELEASE_COEFF` (0.05) — classic VU spec is 300ms attack to 99%
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

### Phase 5: Multi-page Touch UI + NVS Settings

**Files to create**: `ui_manager.cpp`, `ui_manager.h`, `settings.cpp`, `settings.h`

**Pages**:

| Page | Content | Navigation |
|------|---------|------------|
| **Main** | Spectrum / VU visualizations | Tap = cycle viz mode |
| **DAC** | AK4493 volume, filter, sound mode, mute | Swipe from right edge |
| **BLE** | Gear VR connection status, pair/unpair | Swipe from left edge |
| **Settings** | Brightness, ADC sensitivity, mouse sensitivity, about | Long-press |

**UI Framework**:
- Page stack with swipe gesture detection (track touch X delta)
- Swipe threshold: ~50px horizontal movement
- Transition animation: slide left/right (shift sprite content)
- Each page has its own `draw(TFT_eSprite &spr)` and `handleTouch(int x, int y)` methods
- Small page indicator dots at bottom of screen

**NVS Settings** (ESP32 Preferences library):
```
namespace "config":
  viz_mode        (uint8_t)  — last active visualization
  brightness      (uint8_t)  — backlight PWM
  adc_sensitivity (float)    — spectrum sensitivity divisor
  dac_volume_l    (uint8_t)  — AK4493 left volume
  dac_volume_r    (uint8_t)  — AK4493 right volume  
  dac_filter      (uint8_t)  — AK4493 filter mode
  dac_sound_mode  (uint8_t)  — AK4493 sound control
  dac_mute        (bool)     — mute state
  mouse_sens      (float)    — USB HID mouse sensitivity
  mouse_mode      (uint8_t)  — touchpad vs gyro
  ble_bonded_addr (string)   — last paired Gear VR MAC address
```

---

### Phase 6: Dual-core FreeRTOS Task Architecture

**Refactoring goal**: Move from single-threaded `loop()` to FreeRTOS tasks for better real-time performance.

**Task layout**:
```cpp
// Core 1 — Audio & Display (time-critical)
void audioDisplayTask(void *param) {
    while(1) {
        if (audio_sampling_is_ready()) {
            audio_sampling_consume();
            spectrum_compute_fft();
            vu_meter_update(rms, peak);
        }
        drawFrame();
        vTaskDelay(1);  // yield briefly
    }
}

// Core 0 — BLE & USB HID  
void bleHidTask(void *param) {
    while(1) {
        ble_gearvr_poll();       // check connection, parse packets
        usb_hid_send_report();   // send queued HID reports
        vTaskDelay(10);          // ~100 Hz polling
    }
}

// Core 0 — Touch & UI (lower priority)
void touchUiTask(void *param) {
    while(1) {
        ui_manager_poll_touch();
        ui_manager_handle_navigation();
        vTaskDelay(20);  // ~50 Hz is fine for touch
    }
}
```

**Shared data protection**:
- Mutex for AK4493 SPI (if accessed from UI task and other contexts)
- Mutex for I2C bus (touch controller — only accessed from touch task, so likely not needed)
- Atomic/volatile for visualization mode variable (written by touch task, read by display task)
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

*Last updated: Phase 1 complete. Next: Phase 2 (AK4493 SPI driver).*
