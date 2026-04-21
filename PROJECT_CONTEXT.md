# ESP32-S3 Audio Visualizer - Project Context

**Version**: 1.2-alpha (Gear VR BLE + USB HID Mouse integrated)
**Platform**: LilyGo T-Display-S3-Long
**Status**: Audio visualization + Gear VR BLE controller + USB HID Mouse emulation working

---

## ⚠️ IMPORTANT FOR AI ASSISTANTS — READ FIRST

**DO NOT attempt to compile or build this project.** This is an Arduino sketch targeting a physical ESP32-S3 board; it requires:
- Arduino IDE 2.x or arduino-cli with ESP32 core 3.x
- Specific libraries: `NimBLE-Arduino`, `TFT_eSPI` (patched for AXS15231B), `arduinoFFT`, `ArduinoJson`, TinyUSB (`USB.h`, `USBHIDMouse.h`)
- Board: `ESP32S3 Dev Module` with settings: USB CDC On Boot = **Enabled**, USB Mode = **HW CDC+JTAG**, PSRAM = **OPI 8MB**, Flash Size = **16MB**, Partition Scheme = **16M Flash (3MB APP/9.9MB FATFS)**
- Physical hardware (LilyGo T-Display-S3-Long + Gear VR controller) to test behavior

The user flashes the firmware via Arduino IDE and tests interactively. **Your job is to make source code changes only.** Do not run `arduino-cli compile`, `pio run`, `make`, or similar — they will either fail silently due to environment gaps or produce misleading errors unrelated to the actual code.

**Verification workflow**: user loads firmware → observes Serial monitor / mouse behavior → reports back. Iterate on code via focused edits, not rebuilds.

---

## 1. Project Overview

**Goal**: Real-time audio spectrum analyzer and VU meter with capacitive touch control on ESP32-S3, PLUS Samsung Gear VR controller (BLE) acting as USB HID Mouse over the board's native USB.

**Implemented Features**:
1. ✅ Technics EQ (SH-GE70 style) — 10-band FFT spectrum with cyan-to-red gradient
2. ✅ Technics VU (RS-TR373 style) — Dual 16-segment amber VU meters with dB scale
3. ✅ Capacitive touch control for mode switching (EQ ↔ VU)
4. ✅ Auto-brightness via ambient light sensor (GPIO4)
5. ✅ Web Serial UI for real-time settings control
6. ✅ FreeRTOS dual-core architecture (Core 0: touch/serial/BLE/HID, Core 1: audio/display)
7. ✅ PSRAM-optimized display rendering with full-frame 640×180 push
8. ✅ **Samsung Gear VR controller via BLE (NimBLE client)** — 10-bit touchpad + 6 buttons + IMU
9. ✅ **USB HID Mouse emulation** — Gear VR touchpad drives host cursor (FHD-optimized ballistics)

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

### 3.1 Current Module Structure (Phases 1, 3, 4, 5, 6 DONE)

```
ESP32S3_Audio_Visualizer.ino     Main sketch — FreeRTOS task creation, USB/BLE setup, loop
  ├── pins_config.h              All hardware pin definitions and constants
  ├── AXS15231B.cpp/.h           QSPI display driver (PSRAM rotation buffer)
  ├── audio_sampling.cpp/.h      Timer-driven stereo ADC, double-buffer, RMS/peak
  ├── spectrum.cpp/.h            arduinoFFT, 32-band log mapping, stereo bar visualization
  ├── vu_meter.cpp/.h            2 VU styles (Needle, LED Ladder) with stereo ballistics
  ├── serial_cmd.cpp/.h          JSON command handler over USB CDC Serial
  ├── settings.cpp/.h            NVS persistence for all configuration
  ├── light_sensor.cpp/.h        Ambient light ADC → auto backlight PWM
  ├── gearvr_controller.cpp/.h   NimBLE client for Samsung Gear VR + USB HID Mouse integration
settings.html                    Standalone Web Serial UI (opened locally in browser)
.gitignore                       Build artifacts and IDE files
```

**Note**: USB HID logic is NOT in a separate `usb_hid.cpp` — it lives inside `gearvr_controller.cpp` because the movement logic is tightly coupled to the BLE notification callback for zero-latency cursor updates.

### 3.2 Target Module Structure (Remaining Future Additions)

```
  └── ak4493.cpp/.h              AK4493 SPI driver (register read/write, volume, filter)  [Phase 2]
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

### Phase 3: BLE Gear VR Controller Client [DONE]

**Files**: `gearvr_controller.cpp`, `gearvr_controller.h`

**Library**: NimBLE-Arduino (saves ~100 KB RAM vs default ESP-IDF BLE).

**Actual Implementation** (reference: https://github.com/rdady/gear-vr-controller-windows):

- **Hardcoded MAC pairing** (not scanning): `#define GEARVR_MAC_ADDRESS "2C:BA:BA:2A:D4:05"` — user-configurable per controller
- **Service UUID**: `4f63756c-7573-2054-6872-65656d6f7465` ("Oculus Three Remote")
- **Data characteristic**: `c8c51726-81bc-483b-a052-f7a14ea3d281` (subscribe to notifications)
- **Command characteristic**: `c8c51726-81bc-483b-a052-f7a14ea3d282` (write keep-alive)
- **Connection parameters**: `pClient->updateConnParams(6, 12, 0, 400)` → interval 7.5–15 ms → **66–133 Hz packet rate** (critical for smooth mouse)

**Activation sequence** (in `gearvr_connect()`):
1. Connect to MAC
2. Discover services (force refresh: `getServices(true)`)
3. Read battery service (`0x180F`) to wake controller
4. Subscribe to data characteristic notifications
5. Write command `0x01 0x00 0x00` (no response), then `0x01 0x00` (with response)
6. Periodic keep-alive `0x01 0x00 0x00` every 1 s in `gearvr_update()`

**60-byte BLE packet parsing** (in `notifyCallback`):

| Field | Bytes | Formula |
|-------|-------|---------|
| Touchpad X (10-bit, 0–1023) | 54, 55 | `((pData[54] & 0x0F) << 6) \| ((pData[55] & 0xFC) >> 2)` |
| Touchpad Y (10-bit, 0–1023) | 55, 56 | `((pData[55] & 0x03) << 8) \| pData[56]` |
| Accel X/Y/Z (int16 LE) | 4–9 | little-endian `(hi << 8) \| lo` |
| Gyro X/Y/Z (int16 LE) | 10–15 | same |
| Mag X/Y/Z (int16 LE) | 16–21 | same |
| Buttons bitfield | 58 | bit0=Trigger, bit1=Home, bit2=Back, bit3=TouchpadClick, bit4=VolUp, bit5=VolDown |
| Battery % | 59 | raw uint8 |

**Touch detection**: `fingerOnPad = (rawX > 0 \|\| rawY > 0)` — coordinate-based, NOT `bit 3` of byte 58 (that's a *click*, not a *touch*).

**Auto-reconnect**: `gearvr_update()` checks `pClient->isConnected()` every cycle. On disconnect or 10 s data timeout, calls `gearvr_connect()` after 15 s cooldown. All mouse buttons are released on disconnect to prevent stuck state.

---

### Phase 4: USB HID Mouse Output [DONE]

**Files**: `gearvr_controller.cpp` (integrated — no separate `usb_hid.cpp`)

**Library**: `USB.h` + `USBHIDMouse.h` (TinyUSB built into ESP32 Arduino Core 3.x).

**USB descriptor** (in `ESP32S3_Audio_Visualizer.ino` `setup()`):
```cpp
USB.VID(0xCAFE);
USB.PID(0x0001);
USB.productName("ESP32-S3 Audio Visualizer");
USB.manufacturerName("Taito");
USB.begin();
Mouse.begin();
```

**Composite device**: USB CDC (Serial debug) + HID Mouse — simultaneous via TinyUSB. `Mouse.move/press/release` are no-ops if host isn't connected (non-blocking — BLE keeps working even without USB host).

**Button mapping (current)**:

| Gear VR input | Host action | HID class |
|---|---|---|
| Trigger (hold) | Air-mouse gate — cursor only moves while held | — |
| Touchpad click, **L-zone** (x ≤ 160) | LEFT click | Mouse |
| Touchpad click, **R-zone** (x >  160) | RIGHT click | Mouse |
| Touchpad swipe (finger on pad, Y) | Scroll wheel | Mouse |
| Touchpad swipe (finger on pad, X) | Horizontal pan | Mouse |
| Home | MUTE (`0x00E2`) | Consumer |
| Back | PLAY / PAUSE (`0x00CD`) | Consumer |
| Volume + | Volume Increment (`0x00E9`) | Consumer |
| Volume − | Volume Decrement (`0x00EA`) | Consumer |

Touchpad X is reported as 10-bit but the HW produces an effective range of `0..~315`; the zone split therefore sits at `160`, not `512`. The zone is **latched at the rising edge of the click** so a drag mid-hold cannot flip the button. LEFT/RIGHT use **50 ms debounce** via `lastLeftChange/lastRightChange` millis timers.

**Additional Consumer usage codes defined** (not wired to buttons yet, reserved for future gestures / mappings): `SCAN_NEXT_TRACK` (0x00B5), `SCAN_PREV_TRACK` (0x00B6), `AC_BACK` (0x0224), `AC_FORWARD` (0x0225).

**Air-mouse logic** (see Section 8 for full detail):
- Cursor velocity = gyro angular rate × sensitivity (gyro is 0 at rest, so no per-touch recalibration)
- **Bias calibration**: 100-sample warmup at connect (motion-gated — rejects windows where any gyro axis exceeds 150 u), plus slow accel-gated stillness EMA while running.
- **HARD HID clamp**: `sendMouseMove()` clips total dx/dy to ±200 px per call, so a single sensor spike cannot flood the USB HID buffer pool (previously caused `Failed to allocate buffer` storms and silently dropped clicks).
- **Sub-pixel accumulator clamp**: `remainderX/Y` capped at ±200 so spikes can't queue ghost motion for the next frames.
- `truncf()`-based sub-pixel emission (smooth low-speed "oily" feel).
- Hard-subtractive vector deadzone (tremor killed cleanly at the boundary).
- `Mouse.move()` called **directly from BLE `notifyCallback`** for zero-latency — no polling delay.

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

## 8. Mouse Ballistics — Current Tuning (Authoritative)

The pointer is **gyroscope-driven** (angular-rate), not touchpad-delta-driven. Touchpad is reserved for scroll + clicks. All tuning constants live at the top of the USB HID section in `gearvr_controller.cpp` (~line 645):

```cpp
#define AIR_GYRO_DEADZONE    10       // Hard subtractive vector deadzone (tremor floor)
#define AIR_GYRO_SENS        0.08f    // Base sensitivity (per-frame gain on emaDelta)
#define AIR_GYRO_ACCEL_REF   700.0f   // Reference speed for quadratic acceleration onset
#define AIR_GYRO_ACCEL       0.9f     // Quadratic boost factor above reference speed
#define AIR_GYRO_MAX         4000     // Clamp extreme per-sample gyro spikes
#define AIR_EMA_ALPHA        0.40f    // Per-frame low-pass (0.40 = keep 40% new, 60% old)
#define AIR_MOUSE_INVERT_X   true     // Yaw → mouse-X sign
#define AIR_MOUSE_INVERT_Y   true     // Pitch → mouse-Y sign
#define MOUSE_HID_MAX        127      // int8_t max per Mouse.move() chunk
#define MOUSE_MOVE_CLAMP     200      // Total per-call hard clamp (HID flood guard)
```

### 8.1 `handleAirMouse(bool touched)` algorithm (per BLE packet, `touched` = trigger held)

```
 1. If BLE link dead OR bias not yet primed → zero all accumulators, return
 2. If !touched (trigger released) → zero all accumulators EVERY FRAME, return
 3. On first touched frame → seed accel LPF, zero EMA / remainders, record start time
 4. During first 150 ms after touch → keep LPF running but freeze cursor (settle jolt)
 5. If scroll-lock active (finger moving on pad) → freeze cursor
 6. rX = gyroZ - biasZ      // yaw   → mouse-X
    rY = gyroX - biasX      // pitch → mouse-Y (note: Gear VR puts pitch on X-axis)
 7. Optional linear fusion with accel tilt (disabled by default, AIR_FUSION_ENABLE=0)
 8. Apply INVERT_X/Y, clamp to ±AIR_GYRO_MAX
 9. EMA: emaDelta = ALPHA*raw + (1-ALPHA)*emaDelta
10. Hard subtractive vector deadzone:
       mag = √(emaX² + emaY²)
       mag < DZ  → zero both
       mag ≥ DZ  → scale by (mag - DZ) / mag   (keeps diagonals straight, no step)
11. Unified vector gain: gain = SENS * (1 + ACCEL * mag / ACCEL_REF)
12. remainderX += emaX * gain ;  remainderY += emaY * gain
13. **Clamp remainderX/Y to ±200**  (spike can't queue ghost motion for next frames)
14. moveX = truncf(remainderX)  (toward-zero — no step at ±0.5 like roundf)
15. remainderX -= moveX         (fractional part persists → sub-pixel "oily" glide)
16. sendMouseMove(moveX, moveY)
```

### 8.2 `sendMouseMove(int32_t dx, int32_t dy)`

Two-stage defence:

1. **Hard clamp**: `|dx|, |dy| ≤ MOUSE_MOVE_CLAMP (200)`. Without this, a single sensor spike (contolller bumped / re-init) could drive dx into the thousands, producing **hundreds of 127-px HID reports in one call** — which saturates the USB HID buffer pool so subsequent `Mouse.press/release` for **clicks** fail to enqueue (`Failed to allocate buffer, retrying`) and get silently dropped.
2. **int8_t split**: loop emits chunks of ±127 until dx and dy are drained. `Mouse.move()` is an `int8_t` API — a naive cast of a >127 delta wraps negative and the cursor flicks backward.

### 8.3 Why each piece exists (regression guard)

| Symptom | Root cause | Fix anchor |
|---|---|---|
| Clicks logged but never reach host | USB HID pool flooded by spike-driven Mouse.move storm | `MOUSE_MOVE_CLAMP` (§8.2) + `REMAINDER_CLAMP` (step 13) |
| Cursor flies at rest right after connect | Warmup captured a motion sample into bias | Motion-gated warmup: any gyro axis > 150 u aborts and restarts the window |
| Cursor drifts constantly in one direction | Stale accel bias (orientation changed since warmup) | Slow EMA accel bias while accelerometer sees stillness |
| Cursor flicks on finger landing on pad | Hand jolt spikes gyro + accel | 150 ms freeze after touch-begin, LPF kept warm |
| Cursor jitter on small hold | Tremor below deadzone | Hard-subtractive vector deadzone (step 10) |
| Diagonals feel slower than axes | Per-axis independent gains | Unified vector gain using `√(x²+y²)` (step 11) |
| Cursor flicks backward on fast swipe | int8_t overflow from single large Mouse.move | `sendMouseMove` chunked loop + hard clamp |
| Up-is-down | Controller orientation vs screen | Toggle `AIR_MOUSE_INVERT_Y` |
| Right click never triggers | Touchpad X threshold 512 is unreachable (HW range 0…315) | `TP_CLICK_RIGHT_ZONE_X = 160` |

### 8.4 Data flow

```
Gear VR BLE notification (60 B, ~100 Hz)
      ↓
notifyCallback()                              ← NimBLE host thread, Core 0
  ├─ parse bytes 4-21 → accel / gyro (int16 LE)
  ├─ parse bytes 54-56 → touchpad X/Y
  ├─ parse byte 58 → button bitfield
  ├─ update gearVR global struct
  ├─ gyro / accel bias maintenance (warmup + stillness EMA)
  ├─ handleScroll(rawX, rawY, fingerOnPad)   → Mouse.move(0,0, wheel, pan)
  └─ handleAirMouse(trigger)                 → sendMouseMove() → Mouse.move()

gearvr_update_mouse() [loop, ~100 Hz]
  ├─ touchpad click zone latch (L/R) → Mouse LEFT / RIGHT (debounced)
  └─ Home / Back / Volume edges      → ConsumerControl press/release
```

---

*Last updated: Phase 1 + 3 + 4 + 5 + 6 complete. Stereo ADC (GPIO3 L, GPIO4 R), ambient light auto-brightness, dual-core FreeRTOS, float FFT, dynamic DC removal, 2 VU styles, USB Serial + Web Serial settings UI with NVS persistence, **Gear VR BLE client with gyro-based air mouse (trigger-gated), touchpad-zone LEFT/RIGHT clicks, touchpad scroll + pan, and media-key Consumer Controls (Home=MUTE, Back=PLAY/PAUSE, Vol±)**. Next: Phase 2 (AK4493 SPI driver).*

**Reminder for AI assistants**: see top-of-file warning — do NOT attempt to compile. Edit source only; user tests on real hardware.
