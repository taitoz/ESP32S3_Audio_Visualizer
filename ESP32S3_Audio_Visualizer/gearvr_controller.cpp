#include "gearvr_controller.h"
#include <NimBLEDevice.h>
#include "USB.h"
#include "USBHIDMouse.h"
#include "USBHIDConsumerControl.h"
#include <string>
#include <math.h>

// USB HID instances (declared in main .ino)
extern USBHIDMouse Mouse;
extern USBHIDConsumerControl ConsumerControl;

// HID Consumer Control usage codes (USB HID Usage Table, page 0x0C)
#define HID_CC_VOLUME_INCREMENT  0x00E9
#define HID_CC_VOLUME_DECREMENT  0x00EA
#define HID_CC_AC_BACK           0x0224

/*******************************************************************************
 * Gear VR Controller - BLE Implementation
 * 
 * Uses NimBLE stack for low-memory Bluetooth LE.
 * Connects to Gear VR controller by MAC address and subscribes to notifications.
 ******************************************************************************/

// Gear VR Controller MAC address
#define GEARVR_MAC_ADDRESS "2c:ba:ba:2a:d4:05"

// Oculus proprietary service and characteristics
// CORRECTED UUID from actual controller scan: "Oculus Three Remote" (not "Oculus Threedoom")
static BLEUUID serviceUUID("4f63756c-7573-2054-6872-65656d6f7465");  // "Oculus Three Remote"
static BLEUUID dataCharUUID("c8c51726-81bc-483b-a052-f7a14ea3d281");  // Main data stream
static BLEUUID commandCharUUID("c8c51726-81bc-483b-a052-f7a14ea3d282");  // Command channel

// Battery service (standard BLE)
static BLEUUID batteryServiceUUID((uint16_t)0x180F);
static BLEUUID batteryLevelUUID((uint16_t)0x2A19);

volatile GearVRState gearVR = {0};

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pDataChar = nullptr;
static NimBLERemoteCharacteristic* pCommandChar = nullptr;
static NimBLERemoteCharacteristic* pBatteryChar = nullptr;

// Auto-reconnect state
static uint32_t lastConnectAttempt = 0;
static uint32_t lastKeepAlive = 0;

// === AIR MOUSE STATE (Gyroscope-based, angular rate pointing) ===
// Cursor velocity is proportional to gyro angular rate. Gyro naturally outputs 0
// at rest, so no per-touch recalibration needed. A slow-tracking bias subtracts
// any static DC drift that accumulates when the sensor is stationary.

// Static gyro bias (drift compensation, slow exponential tracking while still)
static float gyroBiasX = 0.0f;
static float gyroBiasY = 0.0f;
static float gyroBiasZ = 0.0f;
static bool  gyroBiasPrimed = false;
static uint16_t gyroBiasCount = 0;
#define GYRO_BIAS_WARMUP_SAMPLES  100   // ~1 s averaging window at ~100Hz BLE rate

// Minimal EMA (alpha = 0.9 — kills single-sample sensor noise only)
static float emaDeltaX = 0.0f;
static float emaDeltaY = 0.0f;

// Sub-pixel float accumulators (prevent int-truncation "grid" jitter)
static float remainderX = 0.0f;
static float remainderY = 0.0f;

// Per-touch recalibration (resets "zero" every time finger lands on pad)
//   Frame budget: [PRE_SETTLE skipped] + [SAMPLES accumulated] = total freeze time
//   Pre-settle avoids contaminating bias with the finger-landing transient.
#define TOUCH_RECALIB_PRE_SETTLE   5    // ~50 ms — wait for finger to steady after landing
#define TOUCH_RECALIB_SAMPLES      25   // ~250 ms of averaging for a robust new "zero"
#define TOUCH_RECALIB_TOTAL        (TOUCH_RECALIB_PRE_SETTLE + TOUCH_RECALIB_SAMPLES)
static uint16_t touchRecalibCount = TOUCH_RECALIB_TOTAL;   // not recalibrating initially
static float    touchBiasAccumX = 0.0f;
static float    touchBiasAccumY = 0.0f;
static float    touchBiasAccumZ = 0.0f;

// Scroll-vs-mouse mutual exclusion: fast touchpad motion locks the air mouse
// for a short time so scrolling doesn't fight with gyro cursor movement.
#define SCROLL_LOCK_THRESHOLD  5      // touchpad |dx|+|dy| above this = "scrolling"
#define SCROLL_LOCK_MS         100    // block air mouse for this long after scroll event
static uint32_t scrollLockUntilMs = 0;

// Previous accelerometer values (for "still" detection → force-zero gyro)
static int16_t prevAccelX = 0;
static int16_t prevAccelY = 0;
static int16_t prevAccelZ = 0;
static bool    accelPrimed = false;
#define ACCEL_STILL_THRESHOLD  35   // |Δaccel| below this = hand is still → kill gyro

// Touch state
static bool wasTouched = false;
static uint16_t scrollLastY = 0;
static uint16_t scrollLastX = 0;
static bool scrollInit = false;

// Scroll sub-pixel accumulators (Y = wheel, X = pan/horizontal)
static float scrollRemainder  = 0.0f;   // vertical
static float scrollRemainderX = 0.0f;   // horizontal

// Button state tracking + debounce
static bool mouseLastLeft = false;
static bool mouseLastRight = false;
static bool lastVolUp = false;
static bool lastVolDown = false;
static bool lastBack = false;
static bool lastTouchpadClicked = false;
static uint32_t lastLeftChange = 0;
static uint32_t lastRightChange = 0;
#define BUTTON_DEBOUNCE_MS 50

// Forward declarations
static void handleAirMouse(bool touched);
static void handleScroll(uint16_t touchX, uint16_t touchY, bool touched);

// Release all active mouse buttons (called on disconnect to prevent stuck state)
static void resetMouseState()
{
    if (mouseLastLeft)  { Mouse.release(MOUSE_LEFT);  mouseLastLeft = false; }
    if (mouseLastRight) { Mouse.release(MOUSE_RIGHT); mouseLastRight = false; }
    wasTouched = false;
    scrollInit = false;
    emaDeltaX = 0.0f;
    emaDeltaY = 0.0f;
    scrollRemainder  = 0.0f;
    scrollRemainderX = 0.0f;
    remainderX = remainderY = 0.0f;
    touchRecalibCount = TOUCH_RECALIB_TOTAL;   // no pending recalibration
    touchBiasAccumX = touchBiasAccumY = touchBiasAccumZ = 0.0f;
    scrollLockUntilMs = 0;
    accelPrimed = false;
    prevAccelX = prevAccelY = prevAccelZ = 0;
    gyroBiasPrimed = false;   // re-prime bias on next connect
    gyroBiasCount = 0;
    gyroBiasX = gyroBiasY = gyroBiasZ = 0.0f;
}

// Notification callback for main data stream
static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify)
{
    if (length < 60) {
        return;  // Silently reject short packets (hot path — no Serial)
    }
    
    // === CORRECT 10-BIT PARSING ALGORITHM (from reference implementation) ===
    // Reference: https://github.com/rdady/gear-vr-controller-windows
    
    // Touch Active flag (byte 58, bit 3 = touchpad clicked)
    bool touchActive = (pData[58] & 0x08) > 0;
    
    // X coordinate (10-bit): 4 bits from byte 54 + 6 bits from byte 55
    // axisX = (((byte_values[54] & 0xF) << 6) + ((byte_values[55] & 0xFC) >> 2)) & 0x3FF;
    uint16_t rawX = (((pData[54] & 0x0F) << 6) | ((pData[55] & 0xFC) >> 2)) & 0x3FF;
    
    // Y coordinate (10-bit): 2 bits from byte 55 + 8 bits from byte 56
    // axisY = (((byte_values[55] & 0x3) << 8) + ((byte_values[56] & 0xFF) >> 0)) & 0x3FF;
    uint16_t rawY = (((pData[55] & 0x03) << 8) | pData[56]) & 0x3FF;
    
    // Buttons from byte 58 (reference implementation)
    // triggerButton    = ((byte_values[58] &  1) ==  1);
    // homeButton       = ((byte_values[58] &  2) ==  2);
    // backButton       = ((byte_values[58] &  4) ==  4);
    // touchpadButton   = ((byte_values[58] &  8) ==  8);
    // volumeUpButton   = ((byte_values[58] & 16) == 16);
    // volumeDownButton = ((byte_values[58] & 32) == 32);
    bool trigger = (pData[58] & 0x01) > 0;  // Bit 0
    bool homeBtn = (pData[58] & 0x02) > 0;  // Bit 1
    bool backBtn = (pData[58] & 0x04) > 0;  // Bit 2
    bool touchpadBtn = (pData[58] & 0x08) > 0;  // Bit 3
    bool volUp = (pData[58] & 0x10) > 0;    // Bit 4
    bool volDown = (pData[58] & 0x20) > 0;  // Bit 5
    
    // Battery (byte 59)
    uint8_t battery = pData[59];
    
    // === UPDATE GLOBAL STATE ===
    // Touch is active when coordinates are non-zero (finger on touchpad)
    // touchActive flag (bit 3) is for CLICKING, not touching!
    bool fingerOnPad = (rawX > 0 || rawY > 0);
    
    gearVR.touchActive = fingerOnPad;  // Use coordinate-based detection
    gearVR.triggerPressed = trigger;
    gearVR.homePressed = homeBtn;
    gearVR.backPressed = backBtn;
    gearVR.touchpadClicked = touchpadBtn;  // Bit 3 — physical click on pad
    gearVR.volumeDownPressed = volDown;
    gearVR.volumeUpPressed = volUp;
    gearVR.batteryLevel = battery;
    
    // Always update coordinates
    gearVR.touchX = rawX;
    gearVR.touchY = rawY;
    
    uint32_t now = millis();
    // (Hot-path Serial disabled — debug is emitted once/sec from loop() instead)
    
    // Parse IMU data (bytes 4-27, little-endian int16)
    int16_t aX = (int16_t)((pData[5] << 8) | pData[4]);
    int16_t aY = (int16_t)((pData[7] << 8) | pData[6]);
    int16_t aZ = (int16_t)((pData[9] << 8) | pData[8]);
    int16_t gX = (int16_t)((pData[11] << 8) | pData[10]);
    int16_t gY = (int16_t)((pData[13] << 8) | pData[12]);
    int16_t gZ = (int16_t)((pData[15] << 8) | pData[14]);
    gearVR.accelX = aX; gearVR.accelY = aY; gearVR.accelZ = aZ;
    gearVR.gyroX  = gX; gearVR.gyroY  = gY; gearVR.gyroZ  = gZ;
    gearVR.magX = (int16_t)((pData[17] << 8) | pData[16]);
    gearVR.magY = (int16_t)((pData[19] << 8) | pData[18]);
    gearVR.magZ = (int16_t)((pData[21] << 8) | pData[20]);
    
    gearVR.lastUpdateMs = now;
    
    // === GYRO BIAS (static drift compensation, warmup-then-track) ===
    // Phase 1 (warmup, first ~300ms): accumulate running average of gyro samples.
    //          Cursor is FROZEN during this window (handleAirMouse checks primed).
    // Phase 2 (run): slow EMA tracker that only updates when controller is still.
    if (!gyroBiasPrimed) {
        // Incremental running average: bias_n = bias_(n-1) + (sample - bias_(n-1)) / n
        gyroBiasCount++;
        float invN = 1.0f / (float)gyroBiasCount;
        gyroBiasX += ((float)gX - gyroBiasX) * invN;
        gyroBiasY += ((float)gY - gyroBiasY) * invN;
        gyroBiasZ += ((float)gZ - gyroBiasZ) * invN;
        if (gyroBiasCount >= GYRO_BIAS_WARMUP_SAMPLES) {
            gyroBiasPrimed = true;
            Serial.printf("[GYRO] Bias calibrated: X=%.1f Y=%.1f Z=%.1f (from %u samples)\n",
                          gyroBiasX, gyroBiasY, gyroBiasZ, gyroBiasCount);
        }
    }
    // NOTE: runtime drift-tracking was removed — it progressively absorbed
    // any sustained gyro reading (e.g. controller held slightly tilted) and
    // gradually "shifted the zero" in whichever direction the user was
    // pointing most. Result felt like directional asymmetry (e.g. "down
    // works, up is blocked" or vice-versa after a while). Now we rely
    // solely on the 100-sample warmup bias captured at connect time.
    // If drift becomes noticeable over long sessions, the user can
    // disconnect/reconnect the controller to re-run warmup calibration.
    
    // Reset EMA history on touch-begin (avoid stale leftovers from previous gesture)
    // and trigger per-touch gyro recalibration — redefines "zero" at every new touch
    // so users can rest their arm, re-grip, and start pointing from any orientation.
    if (fingerOnPad && !wasTouched) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        remainderX = 0.0f;
        remainderY = 0.0f;
        touchRecalibCount = 0;
        touchBiasAccumX = 0.0f;
        touchBiasAccumY = 0.0f;
        touchBiasAccumZ = 0.0f;
    }
    
    // === PER-TOUCH RECALIBRATION ===
    // Phase 1 (PRE_SETTLE): skip first few frames — finger-landing transient
    //                       would contaminate the bias (e.g. tiny recoil when
    //                       user presses the pad makes the controller twitch).
    // Phase 2 (accumulate):  running average of gyro readings → new bias.
    // Cursor is frozen for the whole window (handleAirMouse gates on counter).
    if (fingerOnPad && touchRecalibCount < TOUCH_RECALIB_TOTAL) {
        touchRecalibCount++;
        if (touchRecalibCount > TOUCH_RECALIB_PRE_SETTLE) {
            uint16_t n = touchRecalibCount - TOUCH_RECALIB_PRE_SETTLE;
            float invN = 1.0f / (float)n;
            touchBiasAccumX += ((float)gX - touchBiasAccumX) * invN;
            touchBiasAccumY += ((float)gY - touchBiasAccumY) * invN;
            touchBiasAccumZ += ((float)gZ - touchBiasAccumZ) * invN;
            if (touchRecalibCount >= TOUCH_RECALIB_TOTAL) {
                gyroBiasX = touchBiasAccumX;
                gyroBiasY = touchBiasAccumY;
                gyroBiasZ = touchBiasAccumZ;
            }
        }
    }
    
    // === IMMEDIATE SCROLL + AIR MOUSE (order matters!) ===
    // handleScroll runs first: it may set scrollLockUntilMs if finger is
    // moving fast, then handleAirMouse will honor that lock in the same frame.
    handleScroll(rawX, rawY, fingerOnPad);
    handleAirMouse(fingerOnPad);
}

// Client callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  GEAR VR CONTROLLER CONNECTED! ✓      ║");
        Serial.println("╚════════════════════════════════════════╝");
        Serial.printf("MAC: %s\n", GEARVR_MAC_ADDRESS);
        Serial.printf("RSSI: %d dBm\n", pClient->getRssi());
        // Lowest-latency BLE parameters: interval 7.5ms - 11.25ms → ~100-133 Hz sample rate
        // min=6 (6*1.25ms), max=9 (9*1.25ms), slaveLatency=0, supTimeout=400 (4s)
        pClient->updateConnParams(6, 9, 0, 400);
        Serial.println("[BLE] Conn params: 7.5-11.25ms interval (~100-133 Hz), 4s timeout");
        gearVR.connected = true;
    }
    
    void onDisconnect(NimBLEClient* pClient) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  GEAR VR CONTROLLER DISCONNECTED ✗    ║");
        Serial.println("╚════════════════════════════════════════╝");
        gearVR.connected = false;
    }
};

void gearvr_init()
{
    Serial.println("Initializing Gear VR Controller (NimBLE)...");
    
    // Initialize NimBLE
    NimBLEDevice::init("ESP32-S3-Visualizer");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);  // Max power for stable connection
    
    // Create client
    pClient = NimBLEDevice::createClient();
    pClient->setClientCallbacks(new ClientCallbacks());
    pClient->setConnectionParams(6, 12, 0, 400);  // Low latency: 7.5ms-15ms interval, 4s timeout
    pClient->setConnectTimeout(5);
    
    Serial.println("Gear VR Controller initialized. Call gearvr_connect() to connect.");
}

void gearvr_connect()
{
    if (gearVR.connected && pClient && pClient->isConnected()) {
        Serial.println("[BLE] Already connected");
        return;
    }
    
    Serial.println("\n┌─────────────────────────────────────┐");
    Serial.println("│ CONNECTING TO GEAR VR CONTROLLER   │");
    Serial.println("└─────────────────────────────────────┘");
    Serial.printf("Target MAC: %s\n", GEARVR_MAC_ADDRESS);
    Serial.flush();
    
    NimBLEAddress address(std::string(GEARVR_MAC_ADDRESS), BLE_ADDR_PUBLIC);
    
    // Direct connection with extended timeout
    pClient->setConnectTimeout(15);
    
    if (!pClient->connect(address, false)) {
        Serial.println("✗ Connection failed");
        lastConnectAttempt = millis();
        return;
    }
    
    Serial.println("✓ BLE connected");
    vTaskDelay(pdMS_TO_TICKS(500));  // Let connection stabilize
    
    // === STEP 1: DISCOVER ALL SERVICES ===
    Serial.println("[BLE] Step 1: Discovering all services...");
    auto services = pClient->getServices(true);  // true = force refresh
    
    if (services.empty()) {
        Serial.println("✗ No services found!");
        pClient->disconnect();
        lastConnectAttempt = millis();
        return;
    }
    
    Serial.printf("[BLE] Found %d services:\n", services.size());
    for (auto pSvc : services) {
        Serial.printf("  - %s\n", pSvc->getUUID().toString().c_str());
    }
    
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // === STEP 2: READ BATTERY (wakes up controller) ===
    Serial.println("[BLE] Step 2: Reading battery to wake controller...");
    NimBLERemoteService* pBatteryService = pClient->getService(batteryServiceUUID);
    if (pBatteryService != nullptr) {
        pBatteryChar = pBatteryService->getCharacteristic(batteryLevelUUID);
        if (pBatteryChar != nullptr && pBatteryChar->canRead()) {
            std::string value = pBatteryChar->readValue();
            if (value.length() > 0) {
                gearVR.batteryLevel = (uint8_t)value[0];
                Serial.printf("[BLE] Battery: %d%%\n", gearVR.batteryLevel);
            }
        }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // === STEP 3: GET OCULUS SERVICE ===
    Serial.println("[BLE] Step 3: Getting Oculus service...");
    NimBLERemoteService* pService = pClient->getService(serviceUUID);
    if (pService == nullptr) {
        Serial.println("✗ Oculus service not found!");
        Serial.println("  → Controller must be in pairing mode");
        Serial.println("  → Hold Home + Trigger for 5 seconds");
        Serial.println("  → LED should blink multiple colors");
        pClient->disconnect();
        lastConnectAttempt = millis();
        return;
    }
    Serial.println("✓ Oculus service found");
    
    // === STEP 4: GET CHARACTERISTICS ===
    Serial.println("[BLE] Step 4: Getting characteristics...");
    pDataChar = pService->getCharacteristic(dataCharUUID);
    pCommandChar = pService->getCharacteristic(commandCharUUID);
    
    if (pDataChar == nullptr || pCommandChar == nullptr) {
        Serial.println("✗ Characteristics not found!");
        pClient->disconnect();
        lastConnectAttempt = millis();
        return;
    }
    Serial.println("✓ Characteristics found");
    
    // === STEP 5: ENABLE NOTIFICATIONS ===
    Serial.println("[BLE] Step 5: Enabling notifications...");
    if (pDataChar->canNotify()) {
        if (pDataChar->subscribe(true, notifyCallback)) {
            Serial.println("✓ Notifications enabled");
        } else {
            Serial.println("✗ Failed to enable notifications");
        }
    }
    vTaskDelay(pdMS_TO_TICKS(500));  // Critical delay
    
    // === STEP 6: AGGRESSIVE ACTIVATION SEQUENCE ===
    Serial.println("[BLE] Step 6: Sending activation commands...");
    
    // Try 1: 3-byte command (no response)
    if (pCommandChar->canWriteNoResponse()) {
        uint8_t cmd1[] = {0x01, 0x00, 0x00};
        pCommandChar->writeValue(cmd1, sizeof(cmd1), false);
        Serial.println("[BLE] Sent: 01 00 00 (no response)");
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    // Try 2: 2-byte command (with response)
    if (pCommandChar->canWrite()) {
        uint8_t cmd2[] = {0x01, 0x00};
        if (pCommandChar->writeValue(cmd2, sizeof(cmd2), true)) {
            Serial.println("[BLE] Sent: 01 00 (with response) ✓");
        } else {
            Serial.printf("[BLE] Write Error Code: %d\n", pClient->getLastError());
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    gearVR.connected = true;
    lastKeepAlive = millis();
    Serial.println("✓ Gear VR Controller ACTIVATED!");
    Serial.println("  → Waiting for data packets...");
    Serial.println("  → USB HID Mouse ready");
}

void gearvr_disconnect()
{
    if (pClient && pClient->isConnected()) {
        pClient->disconnect();
    }
    gearVR.connected = false;
}

bool gearvr_is_connected()
{
    return gearVR.connected && pClient && pClient->isConnected();
}

void gearvr_update()
{
    // === CHECK CONNECTION STATUS ===
    bool wasConnected = gearVR.connected;
    bool isConnected = (pClient && pClient->isConnected());
    
    // === DETECT DISCONNECT EVENT ===
    if (wasConnected && !isConnected) {
        Serial.println("\n[BLE] ⚠️  Controller disconnected. Searching...");
        
        // Release all mouse buttons (prevent stuck buttons)
        Mouse.release(MOUSE_LEFT);
        Mouse.release(MOUSE_RIGHT);
        Mouse.release(MOUSE_MIDDLE);
        
        // Reset mouse state
        resetMouseState();
        
        // Mark as disconnected
        gearVR.connected = false;
        
        // Trigger immediate reconnect
        lastConnectAttempt = millis() - 16000;  // Force reconnect now
    }
    
    // === AUTO-RECONNECT: Try every 15 seconds if not connected ===
    if (!isConnected) {
        if (millis() - lastConnectAttempt > 15000) {
            Serial.println("[BLE] 🔍 Auto-reconnect: Starting scan...");
            gearvr_connect();
        }
        return;  // Skip keep-alive and timeout checks
    }
    
    // === KEEP-ALIVE: Send command every 1 second ===
    if (millis() - lastKeepAlive > 1000) {
        if (pCommandChar != nullptr && pCommandChar->canWriteNoResponse()) {
            uint8_t keepAlive[] = {0x01, 0x00, 0x00};
            pCommandChar->writeValue(keepAlive, sizeof(keepAlive), false);
            lastKeepAlive = millis();
        }
    }
    
    // === TIMEOUT CHECK: No data for 10 seconds = disconnect ===
    if (gearVR.connected && (millis() - gearVR.lastUpdateMs > 10000)) {
        Serial.println("[BLE] ⏱️  Data timeout (10s), disconnecting...");
        gearvr_disconnect();
        lastConnectAttempt = millis();
    }
}

/*******************************************************************************
 * USB HID Integration — Air Mouse + Touchpad Scroll + Consumer Control
 *
 * Input mapping:
 *   • Cursor       : Gyroscope angular rate (yaw=X, pitch=Y) with slow drift
 *                    bias tracking. Active ONLY while finger touches the pad.
 *   • Scroll wheel : Touchpad vertical delta (ΔY while touched)
 *   • Trigger              → Mouse LEFT click
 *   • Touchpad physical click → Mouse LEFT click (duplicate of trigger)
 *   • Home button          → Mouse RIGHT click
 *   • Back button          → Consumer AC_BACK
 *   • Volume +             → Consumer Volume Increment
 *   • Volume −             → Consumer Volume Decrement
 ******************************************************************************/

// Air Mouse configuration — GYROSCOPE angular-rate pointing
// Cursor velocity = gyro rate × sensitivity. No acceleration curve, no soft zone —
// gyro reads 0 at rest so deadzone can be minimal (just noise floor).
#define AIR_GYRO_DEADZONE    10        // Soft vector deadzone radius (quadratic roll-off below this)
#define AIR_GYRO_SENS        0.05f     // Linear sensitivity
#define AIR_GYRO_ACCEL_REF   800.0f    // Rate at which quadratic term equals linear term (units)
#define AIR_GYRO_ACCEL       0.7f      // Relative weight of quadratic term at reference rate
#define AIR_GYRO_MAX         4000      // Clamp extreme spikes (sensor glitch protection)
#define AIR_EMA_ALPHA        0.55f     // Stronger smoothing — 55% current, 45% history (kills jitter)
#define AIR_MOUSE_INVERT_X   true      // Flip if horizontal axis feels wrong
#define AIR_MOUSE_INVERT_Y   false     // Flip if vertical axis feels wrong
#define MOUSE_HID_MAX        127       // int8_t max per Mouse.move() call

// Scroll configuration
#define SCROLL_SENS          0.05f     // Touchpad Y delta → scroll units
#define SCROLL_DEADZONE      3         // Ignore sub-pixel touchpad noise
#define SCROLL_WRAP_THRESHOLD 500      // Reject coordinate glitches

// Send mouse movement with int8_t overflow protection (split large deltas).
static void sendMouseMove(int32_t dx, int32_t dy)
{
    while (dx != 0 || dy != 0) {
        int8_t stepX = 0;
        int8_t stepY = 0;
        
        if (dx > MOUSE_HID_MAX) {
            stepX = MOUSE_HID_MAX;
            dx -= MOUSE_HID_MAX;
        } else if (dx < -MOUSE_HID_MAX) {
            stepX = -MOUSE_HID_MAX;
            dx += MOUSE_HID_MAX;
        } else {
            stepX = (int8_t)dx;
            dx = 0;
        }
        
        if (dy > MOUSE_HID_MAX) {
            stepY = MOUSE_HID_MAX;
            dy -= MOUSE_HID_MAX;
        } else if (dy < -MOUSE_HID_MAX) {
            stepY = -MOUSE_HID_MAX;
            dy += MOUSE_HID_MAX;
        } else {
            stepY = (int8_t)dy;
            dy = 0;
        }
        
        Mouse.move(stepX, stepY);
    }
}

// === AIR MOUSE: Gyroscope angular-rate pointing, gated by touch ===
// Cursor velocity is directly proportional to gyro rate (minus static bias).
// Gyro is zero at rest, so no per-touch recalibration needed — cursor
// simply stops when hand stops. Axis mapping:
//   gyroZ (yaw rate)   → mouse X  (wrist twist left/right)
//   gyroY (pitch rate) → mouse Y  (wrist tilt up/down)
//   gyroX (roll rate)  → ignored
static void handleAirMouse(bool touched)
{
    // Freeze cursor while bias is still being calibrated (first ~300ms after connect).
    // Without this, an uncalibrated bias would send the cursor off-screen instantly.
    if (!gyroBiasPrimed) {
        wasTouched = touched;
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        return;
    }
    
    if (!touched) {
        wasTouched = false;
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        remainderX = 0.0f;
        remainderY = 0.0f;
        return;
    }
    if (!wasTouched) {
        wasTouched = true;
        remainderX = 0.0f;
        remainderY = 0.0f;
        return;   // skip first frame to let EMA settle
    }
    
    // Freeze while per-touch recalibration is in progress
    if (touchRecalibCount < TOUCH_RECALIB_TOTAL) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        return;
    }
    
    // Freeze while user is actively scrolling (set by handleScroll)
    if (millis() < scrollLockUntilMs) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        remainderX = 0.0f;
        remainderY = 0.0f;
        return;
    }
    
    // Raw gyro minus tracked static bias → angular rate in sensor units
    float rX = (float)gearVR.gyroZ - gyroBiasZ;   // mouse X  ← yaw
    float rY = (float)gearVR.gyroY - gyroBiasY;   // mouse Y  ← pitch
    
    // NOTE: accel-based still-detect was removed here — it caused directional
    // asymmetry (e.g. "up is harder"): slow wrist tilts upward barely change
    // accel magnitude because gravity still projects on Z, so the detector
    // incorrectly zeroed gyro on legitimate slow up-movements.
    // The slow-EMA bias tracker above already absorbs static drift.
    
    if (AIR_MOUSE_INVERT_X) rX = -rX;
    if (AIR_MOUSE_INVERT_Y) rY = -rY;
    
    // Clamp extreme spikes (sensor glitches)
    if (rX >  AIR_GYRO_MAX) rX =  AIR_GYRO_MAX;
    if (rX < -AIR_GYRO_MAX) rX = -AIR_GYRO_MAX;
    if (rY >  AIR_GYRO_MAX) rY =  AIR_GYRO_MAX;
    if (rY < -AIR_GYRO_MAX) rY = -AIR_GYRO_MAX;
    
    // Minimal EMA FIRST — smooths raw noise before any thresholding
    emaDeltaX = AIR_EMA_ALPHA * rX + (1.0f - AIR_EMA_ALPHA) * emaDeltaX;
    emaDeltaY = AIR_EMA_ALPHA * rY + (1.0f - AIR_EMA_ALPHA) * emaDeltaY;
    
    // === SOFT VECTOR DEADZONE (anti-diamond) ===
    // Hard thresholds produce on/off flapping right at the boundary → visible
    // "diamond grid" steps. Instead, apply a smooth quadratic roll-off:
    //   scale = (mag/DZ)²  when mag < DZ   (→ 0 at mag=0, 1 at mag=DZ)
    //   scale = 1          when mag ≥ DZ
    // Low-amplitude noise is multiplied by a tiny scale and disappears into
    // the float accumulator without snapping, while legitimate motion is
    // unaffected. Diagonals stay perfectly smooth.
    float mag = sqrtf(emaDeltaX * emaDeltaX + emaDeltaY * emaDeltaY);
    if (mag < AIR_GYRO_DEADZONE && AIR_GYRO_DEADZONE > 0.0f) {
        float t = mag / (float)AIR_GYRO_DEADZONE;
        float scale = t * t;             // quadratic roll-off
        emaDeltaX *= scale;
        emaDeltaY *= scale;
    }
    
    // === UNIFIED VECTOR GAIN + SUB-PIXEL FLOAT ACCUMULATOR ===
    // Use ONE scalar gain derived from vector magnitude (not per-axis) so both
    // axes always scale by the same factor → diagonals stay on a straight line.
    // Per-axis independent gains caused subtle direction skew at low rates,
    // which manifested as "diamond grid" staircasing.
    //
    // Emission via roundf() (not truncation): rounds to nearest integer symmetrically
    // for positive & negative, so pixel steps fire at every half-unit instead of
    // only at full units → twice the step resolution, cleaner diagonals.
    float vmag = sqrtf(emaDeltaX * emaDeltaX + emaDeltaY * emaDeltaY);
    float gain = AIR_GYRO_SENS * (1.0f + AIR_GYRO_ACCEL * vmag / AIR_GYRO_ACCEL_REF);
    remainderX += emaDeltaX * gain;
    remainderY += emaDeltaY * gain;
    
    int32_t moveX = (int32_t)roundf(remainderX);   // round to nearest (symmetric)
    int32_t moveY = (int32_t)roundf(remainderY);
    remainderX   -= (float)moveX;                   // keep fractional part
    remainderY   -= (float)moveY;
    
    if (moveX != 0 || moveY != 0) {
        sendMouseMove(moveX, moveY);
    }
}

// === TOUCHPAD SCROLL: vertical touchpad delta drives scroll wheel ===
// Only scrolls while finger is on the pad. Uses int32_t delta to prevent
// wrap-around on fast swipes, and a float accumulator for smooth sub-unit scroll.
static void handleScroll(uint16_t touchX, uint16_t touchY, bool touched)
{
    if (!touched) {
        scrollInit = false;
        scrollRemainder  = 0.0f;
        scrollRemainderX = 0.0f;
        return;
    }
    
    // Don't scroll while touchpad is physically clicked (prevents scroll during click-drag)
    if (gearVR.touchpadClicked) {
        scrollInit = false;   // reseed on next non-clicked frame
        return;
    }
    
    // First touch frame after finger landed — seed the reference X/Y, don't scroll yet
    if (!scrollInit) {
        scrollLastX = touchX;
        scrollLastY = touchY;
        scrollInit  = true;
        return;
    }
    
    int32_t dx = (int32_t)touchX - (int32_t)scrollLastX;
    int32_t dy = (int32_t)touchY - (int32_t)scrollLastY;
    scrollLastX = touchX;
    scrollLastY = touchY;
    
    // Reject coordinate glitches (wrap-around)
    if (abs(dx) > SCROLL_WRAP_THRESHOLD || abs(dy) > SCROLL_WRAP_THRESHOLD) return;
    
    // === SCROLL/MOUSE MUTUAL EXCLUSION ===
    // If finger is moving noticeably on the pad → user intent is "scroll",
    // not "point". Lock out gyro air-mouse for SCROLL_LOCK_MS to eliminate
    // the conflict between small finger drift and cursor movement.
    if (abs(dx) >= SCROLL_LOCK_THRESHOLD || abs(dy) >= SCROLL_LOCK_THRESHOLD) {
        scrollLockUntilMs = millis() + SCROLL_LOCK_MS;
    }
    
    // === VERTICAL WHEEL ===
    // Natural direction: finger moves DOWN → content scrolls DOWN → negative wheel
    int8_t vWheel = 0;
    if (abs(dy) >= SCROLL_DEADZONE) {
        float sF = -(float)dy * SCROLL_SENS + scrollRemainder;
        int32_t units = (int32_t)sF;
        scrollRemainder = sF - (float)units;
        if (units >  127) units =  127;
        if (units < -127) units = -127;
        vWheel = (int8_t)units;
    }
    
    // === HORIZONTAL PAN ===
    // Finger right → content scrolls right → positive pan
    int8_t hWheel = 0;
    if (abs(dx) >= SCROLL_DEADZONE) {
        float sF = (float)dx * SCROLL_SENS + scrollRemainderX;
        int32_t units = (int32_t)sF;
        scrollRemainderX = sF - (float)units;
        if (units >  127) units =  127;
        if (units < -127) units = -127;
        hWheel = (int8_t)units;
    }
    
    if (vWheel != 0 || hWheel != 0) {
        Mouse.move(0, 0, vWheel, hWheel);   // 4-arg: x, y, wheel, pan
    }
}

// === BUTTON + CONSUMER CONTROL HANDLER ===
// Polled from loop() at ~100 Hz. Maps:
//   • Trigger + Touchpad click → Mouse LEFT
//   • Home                     → Mouse RIGHT
//   • Back                     → Consumer AC_BACK (edge-triggered)
//   • Volume +/-               → Consumer Volume Inc/Dec (edge-triggered)
void gearvr_update_mouse()
{
    if (!gearVR.connected) return;
    
    uint32_t now = millis();
    
    bool trigger = gearVR.triggerPressed;
    bool tpClick = gearVR.touchpadClicked;
    bool homeBtn = gearVR.homePressed;
    bool backBtn = gearVR.backPressed;
    bool volUp   = gearVR.volumeUpPressed;
    bool volDn   = gearVR.volumeDownPressed;
    
    // --- Button mapping ---
    //   Trigger OR Touchpad-click → LEFT mouse button
    //   Home                      → RIGHT mouse button
    //   Back                      → Consumer AC_BACK
    //   Volume +/-                → Consumer Volume Inc/Dec
    bool wantLeft  = trigger || tpClick;
    bool wantRight = homeBtn;
    
    // Edge-triggered debug prints for all buttons (instant visual feedback)
    static bool dbgLastTrig = false, dbgLastHome = false, dbgLastBack = false;
    static bool dbgLastVolUp = false, dbgLastVolDn = false;
    if (tpClick  && !lastTouchpadClicked) { if (Serial) Serial.println("[BTN] Touchpad → LEFT");  Serial0.println("[BTN] Touchpad → LEFT"); }
    if (trigger  && !dbgLastTrig)         { if (Serial) Serial.println("[BTN] Trigger  → LEFT");  Serial0.println("[BTN] Trigger  → LEFT"); }
    if (homeBtn  && !dbgLastHome)         { if (Serial) Serial.println("[BTN] Home     → RIGHT"); Serial0.println("[BTN] Home     → RIGHT"); }
    if (backBtn  && !dbgLastBack)         { if (Serial) Serial.println("[BTN] Back     → AC_BACK"); Serial0.println("[BTN] Back     → AC_BACK"); }
    if (volUp    && !dbgLastVolUp)        { if (Serial) Serial.println("[BTN] Vol+"); Serial0.println("[BTN] Vol+"); }
    if (volDn    && !dbgLastVolDn)        { if (Serial) Serial.println("[BTN] Vol-"); Serial0.println("[BTN] Vol-"); }
    dbgLastTrig = trigger; dbgLastHome = homeBtn; dbgLastBack = backBtn;
    dbgLastVolUp = volUp; dbgLastVolDn = volDn;
    
    // --- LEFT click (debounced) ---
    if (wantLeft != mouseLastLeft) {
        if (now - lastLeftChange >= BUTTON_DEBOUNCE_MS) {
            if (wantLeft) Mouse.press(MOUSE_LEFT); else Mouse.release(MOUSE_LEFT);
            mouseLastLeft = wantLeft;
            lastLeftChange = now;
        }
    }
    
    // --- RIGHT click (debounced) ---
    if (wantRight != mouseLastRight) {
        if (now - lastRightChange >= BUTTON_DEBOUNCE_MS) {
            if (wantRight) Mouse.press(MOUSE_RIGHT); else Mouse.release(MOUSE_RIGHT);
            mouseLastRight = wantRight;
            lastRightChange = now;
        }
    }
    
    // --- Consumer Control: edge-triggered one-shot press/release ---
    // Back button → AC_BACK (browser "back")
    if (backBtn != lastBack) {
        if (backBtn) ConsumerControl.press(HID_CC_AC_BACK);
        else        ConsumerControl.release();
        lastBack = backBtn;
    }
    
    // Volume + / - (press and release mirror controller state)
    if (volUp != lastVolUp) {
        if (volUp) ConsumerControl.press(HID_CC_VOLUME_INCREMENT);
        else       ConsumerControl.release();
        lastVolUp = volUp;
    }
    if (volDn != lastVolDown) {
        if (volDn) ConsumerControl.press(HID_CC_VOLUME_DECREMENT);
        else       ConsumerControl.release();
        lastVolDown = volDn;
    }
    
    // Track touchpad click edge (for debug only; actual mapping above)
    lastTouchpadClicked = tpClick;
}

// === Legacy getters (kept for API compatibility — no longer used internally) ===
void gearvr_get_mouse_delta(int16_t *dx, int16_t *dy)
{
    *dx = 0;
    *dy = 0;
}

bool gearvr_get_mouse_buttons(bool *left, bool *right, bool *middle)
{
    if (!gearVR.connected) {
        *left = *right = *middle = false;
        return false;
    }
    *left   = mouseLastLeft;
    *right  = mouseLastRight;
    *middle = false;  // No middle button in air mouse mode
    return true;
}
