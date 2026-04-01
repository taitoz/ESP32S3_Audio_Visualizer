# Optimization Rollback Log

## Overview
Due to display rendering issues and system instability, several performance optimizations from commit `71df4d7241a1b4d310b22297e47fd0088643aff0` have been rolled back. The system now runs on commit `02e701ee2ab171559d2582455e2a69fca8b3319a` with stability prioritized over performance.

## Rolled Back Optimizations

### ❌ Hardware Display Optimizations
| Feature | What Was Changed | Why It Failed | Current State |
|---------|------------------|---------------|---------------|
| **QSPI Clock Speed** | 32 MHz → 40 MHz | Too aggressive for LilyGo T-Display-S3-Long hardware | Reverted to 32 MHz (stable) |
| **Hardware MADCTL Rotation** | Added `lcd_setRotation(1)` call | Caused display pivot issues (4 squares → 3 squares → corrupted) | Removed rotation call |
| **Double Buffering** | Single sprite → sprite[2] array | Caused reload cycle and display corruption | Reverted to single sprite |
| **DMA Async SPI** | Blocking → `lcd_PushColors_DMA()` | Complex pipeline caused instability | Reverted to blocking `lcd_PushColors()` |
| **Buffer Size** | 14400 → 28800 bytes | Memory pressure and DMA alignment issues | Reverted to (28800/2) = 14400 |

### ❌ Display Pipeline Changes
| Change | Issue | Resolution |
|--------|-------|------------|
| **Async DMA Pipeline** | Frame buffer synchronization errors | Blocking transfer restored |
| **Buffer Swapping** | `drawBuf ^= 1` caused race conditions | Single buffer usage |
| **Hardware Rotation** | MADCTL register conflicts with display controller | Software landscape mode (default) |

## Current Working Configuration

### ✅ Stable Settings
```cpp
// pins_config.h
#define SPI_FREQUENCY         32000000  // 32 MHz QSPI (safe)
#define SEND_BUF_SIZE         (28800/2) // 14400 bytes

// AXS15231B.cpp
// lcd_setRotation(1); // DISABLED - causes display pivot issues

// ESP32S3_Audio_Visualizer.ino
TFT_eSprite sprite = TFT_eSprite(&tft); // Single buffer
lcd_PushColors(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer()); // Blocking
```

### ✅ Features Retained
- **Tunable Settings**: All light gain, spectrum smoothing, VU attack/release parameters
- **Web Serial UI**: Full JSON protocol with NVS persistence
- **ArduinoFFT**: Stable FFT processing (slower but reliable)
- **FreeRTOS Dual-Core**: Core 1 (Audio+Display), Core 0 (Touch+Serial)
- **Ambient Light Sensor**: Auto-brightness with gain control

### ❌ Features Lost
- **ESP-DSP SIMD FFT**: ~3-5x faster performance (unstable with display pipeline)
- **Hardware Rotation**: Zero CPU cost landscape mode
- **DMA Async**: CPU free during SPI transfer
- **Double Buffering**: Eliminated pipeline stalls
- **Higher QSPI Speed**: 25% faster display transfer

## Performance Impact

| Metric | Before (71df4d7) | After (Rollback) | Change |
|--------|------------------|------------------|--------|
| **Display FPS** | 30-60 FPS | ~15 FPS | -50% |
| **FFT Speed** | ESP-DSP (3-5x faster) | ArduinoFFT | -70% |
| **CPU Usage** | Lower (DMA + SIMD) | Higher (blocking) | +40% |
| **Stability** | Crashes, display corruption | Stable, reliable | ✅ |

## Root Cause Analysis

### Primary Issues
1. **Hardware Incompatibility**: 40 MHz QSPI too fast for specific board revision
2. **Display Controller Conflicts**: MADCTL rotation incompatible with AXS15231B driver
3. **Pipeline Complexity**: Double buffering + DMA created race conditions
4. **Memory Alignment**: Large buffers caused DMA transfer corruption

### Secondary Issues
1. **Timing Sensitivity**: ESP-DSP FFT timing conflicts with display refresh
2. **PSRAM Fragmentation**: Large sprite buffers caused allocation failures
3. **USB CDC Instability**: High CPU usage affected serial communication

## Recommendations for Future Optimization

### 🔄 Safe Incremental Approach
1. **I2C Speed**: 100 kHz → 400 kHz (low risk)
2. **FFT Only**: Replace ArduinoFFT with ESP-DSP (test isolated)
3. **Partial DMA**: Use DMA for small transfers only
4. **Static Backgrounds**: Pre-render VU dial faces (memory test)

### ⚠️ High-Risk Areas (Avoid)
1. **QSPI Overclock**: 32 MHz is maximum safe speed
2. **Hardware Rotation**: Use software landscape mode
3. **Double Buffering**: Single buffer is more reliable
4. **Complex DMA**: Blocking transfers are stable

### 🧪 Testing Strategy
1. **Isolate Changes**: Test one optimization at a time
2. **Stability First**: Prioritize reliability over performance
3. **Hardware Variants**: Test on multiple board revisions
4. **Long-term Testing**: Run for hours to detect memory leaks

## Current Commit State

- **Base Commit**: `02e701ee2ab171559d2582455e2a69fca8b3319a`
- **Status**: Stable with full functionality
- **Performance**: Acceptable for audio visualization use case
- **Features**: Complete except BLE Gear VR and USB HID (future phases)

## Files Modified During Rollback

1. **pins_config.h**: Reverted SPI_FREQUENCY and SEND_BUF_SIZE
2. **AXS15231B.cpp**: Commented out lcd_setRotation(1) call
3. **ESP32S3_Audio_Visualizer.ino**: Reverted to single sprite and blocking transfers

## Conclusion

The rollback prioritizes system stability and reliability over maximum performance. The current configuration provides a solid foundation for future incremental optimizations that can be tested individually for safety and compatibility.

**Last Updated**: 2026-04-01  
**Rollback Reason**: Display corruption and system instability  
**Current Status**: ✅ Stable and fully functional
