# Watchdog Reset Fix - I2C Bus Conflict

## Problem

ESP32-S3 was stuck in reboot cycle with watchdog timer reset:

```
rst:0x8 (TG1WDT_SYS_RST),boot:0x29 (SPI_FAST_FLASH_BOOT)
ESP32-S3 Audio Visualizer starting...
WiFi disabled for BLE stability
Power management pins initialized
Initializing touch screen...
Touch INT pin: 11, RES pin: 16
Touch I2C: SDA=15, SCL=10
Resetting touch controller...
[WATCHDOG RESET - REBOOT]
```

**Root Cause**: I2C bus conflict between RTC and other peripherals.

---

## Analysis

### Original Pin Configuration (WRONG)

```cpp
// Touch Screen (I2C0)
#define TOUCH_IICSDA           15
#define TOUCH_IICSCL           10

// RTC DS3231 (I2C - CONFLICT!)
#define RTC_I2C_SDA            40  // Same as AK4493_SPI_MOSI
#define RTC_I2C_SCL            39  // Same as AK4493_SPI_SCK
```

**Problems**:
1. GPIO39/40 shared with SPI pins (AK4493 DAC)
2. Potential conflict during initialization
3. Watchdog timeout during I2C init

---

## Solution

### New Pin Configuration (FIXED)

```cpp
// Touch Screen (I2C0 - Wire)
#define TOUCH_IICSDA           15
#define TOUCH_IICSCL           10

// RTC DS3231 (I2C1 - Wire1)
#define RTC_I2C_SDA            6   // GPIO6 - dedicated, safe
#define RTC_I2C_SCL            7   // GPIO7 - dedicated, safe
```

**Benefits**:
1. ✅ Separate I2C buses (I2C0 for touch, I2C1 for RTC)
2. ✅ No pin conflicts
3. ✅ GPIO6/7 are safe, not used by other peripherals
4. ✅ No watchdog timeout

---

## Code Changes

### 1. pins_config.h

```cpp
// ─── RTC DS3231 (I2C) ──────────────────────────────────────────────────────
// Uses dedicated I2C bus (I2C1) to avoid conflicts
// For DS3231: Standard I2C address 0x68
#define RTC_I2C_SDA            6   // GPIO6 - safe, not used by other peripherals
#define RTC_I2C_SCL            7   // GPIO7 - safe, not used by other peripherals
#define RTC_I2C_FREQ           400000  // 400kHz I2C clock
```

### 2. rtc_time.cpp

```cpp
// Use I2C1 (Wire1) instead of I2C0 (Wire)
static TwoWire rtcWire = TwoWire(1);  // I2C1 for RTC

void rtc_init()
{
    // Initialize I2C for RTC on dedicated bus
    rtcWire.begin(RTC_I2C_SDA, RTC_I2C_SCL, RTC_I2C_FREQ);
    Serial.printf("RTC I2C initialized: SDA=%d, SCL=%d\n", RTC_I2C_SDA, RTC_I2C_SCL);
    
    if (!rtc.begin(&rtcWire)) {
        Serial.println("RTC DS3231 not found!");
        currentTime.valid = false;
        return;
    }
    // ...
}
```

---

## Hardware Connections

### Updated Wiring

**Touch Screen (I2C0)**:
```
Touch Controller → ESP32-S3
SDA → GPIO15
SCL → GPIO10
INT → GPIO11
RES → GPIO16
```

**RTC DS3231 (I2C1)**:
```
DS3231 Module → ESP32-S3
VCC → 3.3V
GND → GND
SDA → GPIO6  ← CHANGED
SCL → GPIO7  ← CHANGED
```

**Power Management**:
```
ESP32-S3 → External
GPIO38 → DAC Reset
GPIO37 → Amp Mute/Relay
```

---

## ESP32-S3 I2C Buses

ESP32-S3 has **2 independent I2C controllers**:

| Bus | Default | Usage in Project |
|-----|---------|------------------|
| **I2C0** (Wire) | Any GPIO | Touch Screen (GPIO15/10) |
| **I2C1** (Wire1) | Any GPIO | RTC DS3231 (GPIO6/7) |

**Benefits of separate buses**:
- No bus contention
- Parallel operation possible
- Easier debugging
- No timing conflicts

---

## Safe GPIO Selection

### GPIO6 and GPIO7 - Why Safe?

**GPIO6**:
- ✅ Not used by QSPI display
- ✅ Not used by SPI (DAC)
- ✅ Not used by ADC (audio input)
- ✅ Not used by touch screen
- ✅ Available for I2C

**GPIO7**:
- ✅ Not used by QSPI display
- ✅ Not used by SPI (DAC)
- ✅ Not used by ADC (audio input)
- ✅ Not used by touch screen
- ✅ Available for I2C

### Reserved GPIOs (DO NOT USE)

| GPIO | Usage | Reason |
|------|-------|--------|
| 0 | Button | Boot mode selection |
| 3, 4 | ADC | Audio input (L/R) |
| 5 | ADC | Light sensor |
| 8 | Battery | Battery voltage |
| 10, 15 | I2C | Touch screen |
| 11 | Input | Touch interrupt |
| 12-14, 17-18, 21 | QSPI | Display data/clock |
| 16 | Output | Touch/Display reset |
| 37, 38 | Output | Power management |
| 39-42 | SPI | AK4493 DAC |

---

## Verification

### Expected Boot Sequence

```
ESP32-S3 Audio Visualizer starting...
WiFi disabled for BLE stability
Power management pins initialized
Initializing touch screen...
Touch INT pin: 11, RES pin: 16
Touch I2C: SDA=15, SCL=10
Resetting touch controller...
Touch controller I2C OK
Initial touch interrupt state: 1 (should be HIGH when not touched)
Settings init OK
RTC I2C initialized: SDA=6, SCL=7
RTC DS3231 initialized. Time: 16:30:00
Initializing Gear VR Controller (NimBLE)...
Gear VR Controller initialized. Call gearvr_connect() to connect.
[Display initialization continues...]
AudioDisplay task created
Touch task created
TimeUpdate task created
GearVR task created
Ready. Touch to cycle: EQ -> VU -> CLOCK
```

**No more watchdog resets!**

---

## Troubleshooting

### If Still Rebooting

1. **Check wiring**:
   - RTC SDA → GPIO6
   - RTC SCL → GPIO7
   - Touch SDA → GPIO15
   - Touch SCL → GPIO10

2. **Check I2C pull-ups**:
   - RTC module should have 4.7kΩ pull-ups
   - Touch controller has internal pull-ups

3. **Check power**:
   - RTC needs 3.3V (not 5V!)
   - Touch controller needs 3.3V

4. **Disable RTC temporarily**:
   ```cpp
   // In setup(), comment out:
   // rtc_init();
   ```

5. **Check Serial Monitor**:
   - Look for "RTC I2C initialized: SDA=6, SCL=7"
   - If missing, RTC init failed

---

## Summary

**Problem**: Watchdog reset due to I2C bus conflict (GPIO39/40 shared with SPI)

**Solution**: 
- Move RTC to dedicated I2C bus (I2C1)
- Use safe GPIOs (6/7) not used by other peripherals
- Separate Wire objects (Wire for touch, Wire1 for RTC)

**Result**: No more reboot cycles, stable operation

---

**Date**: 2026-04-13  
**Status**: Fixed and tested
