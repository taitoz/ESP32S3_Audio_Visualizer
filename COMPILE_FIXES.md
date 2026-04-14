# Compilation Fixes

## NimBLE API Compatibility

### Issue 1: NimBLEAddress Constructor

**Error**:
```
error: no matching function for call to 'NimBLEAddress::NimBLEAddress(const char [18])'
```

**Cause**: NimBLE library expects `std::string` for MAC address, not `const char*`.

**Fix**:
```cpp
// Before (wrong):
NimBLEAddress address(GEARVR_MAC_ADDRESS);

// After (correct):
NimBLEAddress address(std::string(GEARVR_MAC_ADDRESS), BLE_ADDR_PUBLIC);
// Note: NimBLE requires 2 arguments: address string + type (BLE_ADDR_PUBLIC or BLE_ADDR_RANDOM)
```

---

### Issue 2: readUInt8() Method Not Found

**Error**:
```
error: 'class NimBLERemoteCharacteristic' has no member named 'readUInt8'
```

**Cause**: NimBLE uses `readValue()` which returns `std::string`, not `readUInt8()`.

**Fix**:
```cpp
// Before (wrong):
gearVR.batteryLevel = pBatteryChar->readUInt8();

// After (correct):
std::string value = pBatteryChar->readValue();
if (value.length() > 0) {
    gearVR.batteryLevel = (uint8_t)value[0];
}
```

---

## Summary of Changes

**File**: `gearvr_controller.cpp`

1. Added `#include <string>` for `std::string` support
2. Changed MAC address constructor: `NimBLEAddress(std::string(GEARVR_MAC_ADDRESS))`
3. Replaced `readUInt8()` with `readValue()` + cast in 2 places:
   - `gearvr_connect()` - initial battery read
   - `gearvr_update()` - periodic battery update

---

## NimBLE API Reference

### Correct Usage

**MAC Address**:
```cpp
// Gear VR uses public BLE address
NimBLEAddress address(std::string("2c:ba:ba:2a:d4:05"), BLE_ADDR_PUBLIC);

// For random addresses (some devices):
// NimBLEAddress address(std::string("aa:bb:cc:dd:ee:ff"), BLE_ADDR_RANDOM);
```

**Reading Characteristics**:
```cpp
std::string value = characteristic->readValue();
uint8_t byte = (uint8_t)value[0];  // First byte
uint16_t word = (value[0] << 8) | value[1];  // Two bytes
```

**Writing Characteristics**:
```cpp
uint8_t data[] = {0x01, 0x00};
characteristic->writeValue(data, sizeof(data), false);
```

---

## Compilation Should Now Succeed

All NimBLE API compatibility issues resolved. Code should compile without errors.
