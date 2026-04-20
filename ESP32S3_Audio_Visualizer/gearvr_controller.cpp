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

// === AIR MOUSE STATE (Accelerometer-based, tilt pointing) ===
// Cursor is driven by accelerometer TILT while finger touches the pad.
// On every touch-begin we snapshot current accel as the "zero reference",
// then delta = current - offset drives cursor movement directly (no heavy filter).

// Per-touch offset (zero reference, snapshotted at rising edge of touch)
static int16_t offsetAccelX = 0;
static int16_t offsetAccelY = 0;
static int16_t offsetAccelZ = 0;

// Light EMA (alpha = 0.8 → nearly pass-through, only kills single-sample noise)
static float emaDeltaX = 0.0f;
static float emaDeltaY = 0.0f;

// Touch state
static bool wasTouched = false;
static uint16_t scrollLastY = 0;
static bool scrollInit = false;

// Zoned click state (latched at click press edge)
static uint16_t clickZoneX = 0;
static bool lastTouchpadClickEdge = false;

// Scroll sub-pixel accumulator (cursor is direct pass-through, no remainders)
static float scrollRemainder = 0.0f;

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
    scrollRemainder = 0.0f;
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
    
    // === INSTANT ACCEL OFFSET SNAPSHOT (on touch-begin) ===
    // At the moment finger lands on the pad, remember current accelerometer
    // values as the "zero reference". From here, cursor reacts to TILT DELTA
    // (current - offset) — so cursor always starts from the current orientation
    // without any drift, calibration window, or heavy filtering.
    if (fingerOnPad && !wasTouched) {
        offsetAccelX = aX;
        offsetAccelY = aY;
        offsetAccelZ = aZ;
        emaDeltaX = 0.0f;   // reset EMA history for clean start
        emaDeltaY = 0.0f;
    }
    
    // === ZONED CLICK: latch zone at CLICK press edge ===
    if (touchpadBtn && !lastTouchpadClickEdge) {
        clickZoneX = rawX;
    }
    lastTouchpadClickEdge = touchpadBtn;
    
    // === IMMEDIATE AIR MOUSE + SCROLL (zero latency, no hot-path Serial) ===
    handleAirMouse(fingerOnPad);
    handleScroll(rawX, rawY, fingerOnPad);
}

// Client callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  GEAR VR CONTROLLER CONNECTED! ✓      ║");
        Serial.println("╚════════════════════════════════════════╝");
        Serial.printf("MAC: %s\n", GEARVR_MAC_ADDRESS);
        Serial.printf("RSSI: %d dBm\n", pClient->getRssi());
        // Optimize BLE connection parameters for lowest latency (7.5ms - 15ms interval)
        // This enables 60-100 Hz data rate from the controller
        pClient->updateConnParams(6, 12, 0, 400);
        Serial.println("[BLE] Connection params updated: interval 7.5-15ms, timeout 4000ms");
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
 *   • Cursor       : Gyro (rotation) — active ONLY while finger touches pad
 *   • Scroll wheel : Touchpad vertical delta (ΔY while touched)
 *   • Touchpad click (left half, X<=512)  → Mouse LEFT click
 *   • Touchpad click (right half, X>512)  → Mouse RIGHT click
 *   • Trigger                             → Mouse LEFT click (dup for convenience)
 *   • Back button                         → Consumer AC_BACK (browser back)
 *   • Volume +                            → Consumer Volume Increment
 *   • Volume −                            → Consumer Volume Decrement
 ******************************************************************************/

// Air Mouse configuration — accelerometer tilt pointing
#define AIR_ACCEL_DEADZONE   150       // Ignore tilt delta below this (tremor)
#define AIR_ACCEL_SENS       0.025f    // Tilt delta → cursor pixels per BLE frame
#define AIR_MAX_DELTA        4000      // Clamp tilt delta (prevents runaway on fast flicks)
#define AIR_EMA_ALPHA        0.8f      // Light EMA — 80% current sample, 20% history
#define AIR_MOUSE_INVERT_X   true      // Flip if axis feels wrong
#define AIR_MOUSE_INVERT_Y   false
#define MOUSE_HID_MAX        127       // int8_t max per Mouse.move() call

// Scroll configuration
#define SCROLL_SENS          0.05f     // Touchpad Y delta → scroll units
#define SCROLL_DEADZONE      3         // Ignore sub-pixel touchpad noise
#define SCROLL_WRAP_THRESHOLD 500      // Reject coordinate glitches

// Touchpad click zoning (X ∈ 0..1023)
#define TOUCH_ZONE_LEFT      400       // X < 400  → LEFT click
#define TOUCH_ZONE_RIGHT     600       // X > 600  → RIGHT click
                                       // 400..600 → NO click (dead center)

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

// === AIR MOUSE: Accelerometer-tilt pointing, gated by touch ===
// Cursor moves ONLY while finger touches the pad. On touch-begin we snapshot
// the current accel as the zero reference (offsetAccelX/Y), then feed the
// delta DIRECTLY to Mouse.move() — no calibration window, no heavy EMA.
static void handleAirMouse(bool touched)
{
    if (!touched) {
        // Finger released — freeze cursor, clear state
        wasTouched = false;
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        return;
    }
    
    // First touch frame — just arm the state (offset already snapshotted in notifyCallback)
    if (!wasTouched) {
        wasTouched = true;
        return;
    }
    
    // Pull current accelerometer values from the struct (already-parsed this packet)
    int16_t aX = gearVR.accelX;
    int16_t aZ = gearVR.accelZ;
    
    // Accelerometer axis mapping for tilt pointing (derived from 4-direction test):
    //   accel X axis → mouse X (left/right tilt)
    //   accel Z axis → mouse Y (up/down tilt)
    int16_t rawDx = (int16_t)(aX - offsetAccelX);
    int16_t rawDy = (int16_t)(aZ - offsetAccelZ);
    
    if (AIR_MOUSE_INVERT_X) rawDx = -rawDx;
    if (AIR_MOUSE_INVERT_Y) rawDy = -rawDy;
    
    // Clamp extreme spikes (noisy sensor glitches) before deadzone check
    if (rawDx >  AIR_MAX_DELTA) rawDx =  AIR_MAX_DELTA;
    if (rawDx < -AIR_MAX_DELTA) rawDx = -AIR_MAX_DELTA;
    if (rawDy >  AIR_MAX_DELTA) rawDy =  AIR_MAX_DELTA;
    if (rawDy < -AIR_MAX_DELTA) rawDy = -AIR_MAX_DELTA;
    
    // Deadzone — ignore tiny tilts (hand tremor)
    int16_t dzDx = (abs(rawDx) < AIR_ACCEL_DEADZONE) ? 0 : rawDx;
    int16_t dzDy = (abs(rawDy) < AIR_ACCEL_DEADZONE) ? 0 : rawDy;
    
    // Light EMA (alpha=0.8 — nearly pass-through, just kills single-sample spikes)
    emaDeltaX = AIR_EMA_ALPHA * (float)dzDx + (1.0f - AIR_EMA_ALPHA) * emaDeltaX;
    emaDeltaY = AIR_EMA_ALPHA * (float)dzDy + (1.0f - AIR_EMA_ALPHA) * emaDeltaY;
    
    // Direct scale to mouse pixels (int16_t math, no heavy accumulators)
    int16_t moveX = (int16_t)(emaDeltaX * AIR_ACCEL_SENS);
    int16_t moveY = (int16_t)(emaDeltaY * AIR_ACCEL_SENS);
    
    if (moveX != 0 || moveY != 0) {
        sendMouseMove((int32_t)moveX, (int32_t)moveY);
    }
}

// === TOUCHPAD SCROLL: vertical touchpad delta drives scroll wheel ===
// Only scrolls while finger is on the pad. Uses int32_t delta to prevent
// wrap-around on fast swipes, and a float accumulator for smooth sub-unit scroll.
static void handleScroll(uint16_t touchX, uint16_t touchY, bool touched)
{
    (void)touchX;  // not used in scroll, but kept for signature symmetry
    
    if (!touched) {
        scrollInit = false;
        scrollRemainder = 0.0f;
        return;
    }
    
    // First touch frame after finger landed — seed the reference Y, don't scroll yet
    if (!scrollInit) {
        scrollLastY = touchY;
        scrollInit = true;
        return;
    }
    
    int32_t dy = (int32_t)touchY - (int32_t)scrollLastY;
    scrollLastY = touchY;
    
    // Reject coordinate glitches (wrap-around)
    if (abs(dy) > SCROLL_WRAP_THRESHOLD) return;
    
    // Deadzone
    if (abs(dy) < SCROLL_DEADZONE) return;
    
    // Accumulate with remainder so fractional scroll counts aren't lost
    // Note: natural scroll direction — finger moves DOWN on pad → content scrolls DOWN
    // (invert sign to match standard mouse wheel: down = negative wheel)
    float scrollF = -(float)dy * SCROLL_SENS + scrollRemainder;
    int32_t scrollUnits = (int32_t)scrollF;
    scrollRemainder = scrollF - (float)scrollUnits;
    
    if (scrollUnits != 0) {
        // Clamp scroll to int8_t range per HID report
        if (scrollUnits > 127)  scrollUnits = 127;
        if (scrollUnits < -127) scrollUnits = -127;
        Mouse.move(0, 0, (int8_t)scrollUnits);
    }
}

// === BUTTON + CONSUMER CONTROL HANDLER ===
// Polled from loop() at ~100 Hz. Maps:
//   • Trigger           → Mouse LEFT
//   • Touchpad click    → Mouse LEFT (if X<=512) or RIGHT (if X>512) — resolved at press edge
//   • Back              → Consumer AC_BACK (one-shot on press edge)
//   • Volume +/-        → Consumer Volume Inc/Dec (one-shot on press edge)
void gearvr_update_mouse()
{
    if (!gearVR.connected) return;
    
    uint32_t now = millis();
    
    bool trigger   = gearVR.triggerPressed;
    bool tpClick   = gearVR.touchpadClicked;
    bool isTouched = gearVR.touchActive;
    bool backBtn   = gearVR.backPressed;
    bool volUp     = gearVR.volumeUpPressed;
    bool volDn     = gearVR.volumeDownPressed;
    
    // --- Compute desired LEFT/RIGHT state ---
    // Touchpad click ONLY registers when finger is actively on the pad AND in a
    // valid zone (X<400 = LEFT, X>600 = RIGHT, 400..600 = dead / ignored).
    // Trigger is independent and always maps to LEFT.
    bool tpLeft  = tpClick && isTouched && (clickZoneX <  TOUCH_ZONE_LEFT);
    bool tpRight = tpClick && isTouched && (clickZoneX >  TOUCH_ZONE_RIGHT);
    bool wantLeft  = trigger || tpLeft;
    bool wantRight = tpRight;
    
    // Hard safety: if finger is not on pad OR click bit is down, force-release
    // any touchpad-zoned press to avoid stuck buttons (trigger still honored).
    if (!isTouched || !tpClick) {
        if (mouseLastLeft && !trigger) {
            Mouse.release(MOUSE_LEFT);
            mouseLastLeft = false;
        }
        if (mouseLastRight) {
            Mouse.release(MOUSE_RIGHT);
            mouseLastRight = false;
        }
    }
    
    // Debug on click edge (one-shot, not in hot path)
    if (tpClick && !lastTouchpadClicked) {
        const char* zone = (clickZoneX < TOUCH_ZONE_LEFT) ? "LEFT"
                         : (clickZoneX > TOUCH_ZONE_RIGHT) ? "RIGHT" : "DEAD";
        if (Serial) Serial.printf("[CLICK] X=%u zone=%s touch=%d\n", clickZoneX, zone, isTouched);
    }
    
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
