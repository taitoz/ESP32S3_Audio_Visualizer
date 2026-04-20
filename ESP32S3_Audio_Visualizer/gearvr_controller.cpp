#include "gearvr_controller.h"
#include <NimBLEDevice.h>
#include "USB.h"
#include "USBHIDMouse.h"
#include <string>

// USB HID Mouse instance (declared in main .ino)
extern USBHIDMouse Mouse;

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

// Previous touchpad position for delta calculation
static uint16_t lastTouchX = 0;
static uint16_t lastTouchY = 0;

// Auto-reconnect state
static uint32_t lastConnectAttempt = 0;
static uint32_t lastKeepAlive = 0;

// USB HID Mouse state tracking
static uint16_t mouseLastX = 0;
static uint16_t mouseLastY = 0;
static bool mouseLastLeft = false;
static bool mouseLastRight = false;
static bool mouseLastMiddle = false;
static bool wasTouched = false;  // Track if touch was active in previous frame

// Button debounce timers (prevent false triggers)
static uint32_t lastLeftChange = 0;
static uint32_t lastRightChange = 0;
static uint32_t lastMiddleChange = 0;
#define BUTTON_DEBOUNCE_MS 50  // 50ms debounce

// Float remainder for sub-pixel precision (anti-jitter)
static float remainderX = 0.0f;
static float remainderY = 0.0f;

// Forward declaration (handleMouse is called in notifyCallback before its definition)
static void handleMouse(int32_t x, int32_t y, bool touched);

// Helper function to reset mouse state
static void resetMouseState()
{
    mouseLastX = 0;
    mouseLastY = 0;
    mouseLastLeft = false;
    mouseLastRight = false;
    mouseLastMiddle = false;
    wasTouched = false;
    remainderX = 0.0f;
    remainderY = 0.0f;
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
    gearVR.volumeDownPressed = volDown;
    gearVR.volumeUpPressed = volUp;
    gearVR.batteryLevel = battery;
    
    // Always update coordinates
    gearVR.touchX = rawX;
    gearVR.touchY = rawY;
    
    // === THROTTLED DEBUG OUTPUT (500ms) ===
    static uint32_t lastDebugPrint = 0;
    uint32_t now = millis();
    if (now - lastDebugPrint >= 500) {
        if (fingerOnPad) {
            Serial.printf("[BLE] X=%d Y=%d Trig=%d Bat=%d\n", rawX, rawY, trigger, battery);
        }
        lastDebugPrint = now;
    }
    
    // Parse IMU data (bytes 4-27, little-endian int16)
    gearVR.accelX = (int16_t)((pData[5] << 8) | pData[4]);
    gearVR.accelY = (int16_t)((pData[7] << 8) | pData[6]);
    gearVR.accelZ = (int16_t)((pData[9] << 8) | pData[8]);
    
    gearVR.gyroX = (int16_t)((pData[11] << 8) | pData[10]);
    gearVR.gyroY = (int16_t)((pData[13] << 8) | pData[12]);
    gearVR.gyroZ = (int16_t)((pData[15] << 8) | pData[14]);
    
    gearVR.magX = (int16_t)((pData[17] << 8) | pData[16]);
    gearVR.magY = (int16_t)((pData[19] << 8) | pData[18]);
    gearVR.magZ = (int16_t)((pData[21] << 8) | pData[20]);
    
    gearVR.lastUpdateMs = now;
    
    // === IMMEDIATE MOUSE UPDATE (zero latency) ===
    // Process mouse movement directly in BLE callback for minimal latency.
    // This avoids waiting for the next loop() poll cycle.
    handleMouse((int32_t)rawX, (int32_t)rawY, fingerOnPad);
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
 * USB HID Mouse Integration
 ******************************************************************************/

// Professional Trackpad Configuration (Power Curve for FHD)
#define MOUSE_DEADZONE 3              // Ignore raw deltas < 3 units (ADC noise)
#define MOUSE_BASE_SENS 0.4f          // Base sensitivity (linear term) - boosted
#define MOUSE_ACCEL_FACTOR 0.025f     // Quadratic acceleration coefficient - boosted
#define MOUSE_WRAP_THRESHOLD 500      // Detect false coordinate jumps
#define MOUSE_HID_MAX 127             // int8_t max for Mouse.move() per call
#define MOUSE_INVERT_X false          // Don't invert X axis
#define MOUSE_INVERT_Y false          // Invert Y (Gear VR Y decreases upward)

// Send mouse movement with int8_t overflow protection.
// Mouse.move() accepts int8_t (-127..127). If delta exceeds range,
// we split it into multiple calls to prevent wrap-around (backward flick).
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

// Clean handleMouse() function - processes touch state and moves cursor.
// Parameters: absolute touch coords (0-1024), touched flag.
static void handleMouse(int32_t x, int32_t y, bool touched)
{
    // === TOUCH STATE MACHINE ===
    if (!touched) {
        // Finger released - reset state
        wasTouched = false;
        remainderX = 0.0f;
        remainderY = 0.0f;
        return;
    }
    
    // First touch frame - initialize lastX/Y, do NOT move cursor (prevents jump)
    if (!wasTouched) {
        mouseLastX = x;
        mouseLastY = y;
        wasTouched = true;
        return;
    }
    
    // === CALCULATE RAW DELTA (int32_t to prevent overflow) ===
    int32_t dx = x - (int32_t)mouseLastX;
    int32_t dy = y - (int32_t)mouseLastY;
    
    // === WRAP-AROUND PROTECTION ===
    // Reject impossibly large jumps (coordinate glitches at boundaries)
    if (abs(dx) > MOUSE_WRAP_THRESHOLD || abs(dy) > MOUSE_WRAP_THRESHOLD) {
        mouseLastX = x;
        mouseLastY = y;
        return;
    }
    
    // === DEADZONE (noise filter) ===
    // Ignore if BOTH axes are below threshold
    if (abs(dx) < MOUSE_DEADZONE && abs(dy) < MOUSE_DEADZONE) {
        return;  // Don't update lastX/Y - allow accumulation on next frame
    }
    
    // === Y-AXIS INVERSION ===
    if (MOUSE_INVERT_X) dx = -dx;
    if (MOUSE_INVERT_Y) dy = -dy;
    
    // === DYNAMIC BALLISTICS (Combined Vector-Length Scale) ===
    // Use a SINGLE scale based on total vector length for both axes.
    // This eliminates the "diamond" effect where diagonals felt slower.
    // Formula: scale = base + |velocity| * accel
    //          move = delta * scale   (quadratic because scale grows with |delta|)
    float velocity = sqrtf((float)(dx * dx + dy * dy));
    float combinedScale = MOUSE_BASE_SENS + (velocity * MOUSE_ACCEL_FACTOR);
    
    float moveX = (float)dx * combinedScale;
    float moveY = (float)dy * combinedScale;
    
    // === FLOATING POINT ACCUMULATOR (anti-jitter, sub-pixel precision) ===
    // remainderX/Y are static globals - persist between calls
    moveX += remainderX;
    moveY += remainderY;
    
    int32_t finalDx = (int32_t)moveX;
    int32_t finalDy = (int32_t)moveY;
    
    // Keep fractional part for next frame
    remainderX = moveX - (float)finalDx;
    remainderY = moveY - (float)finalDy;
    
    // === SEND TO USB HID (with int8_t overflow protection via loop) ===
    // Send FIRST — before any Serial I/O — to minimize latency.
    if (finalDx != 0 || finalDy != 0) {
        sendMouseMove(finalDx, finalDy);
    }
    
    // === THROTTLED DEBUG OUTPUT (once per 500ms, AFTER mouse move) ===
    // Serial over USB-OTG is slow; keep it out of the hot path.
    static uint32_t lastMouseDebug = 0;
    uint32_t nowMs = millis();
    if (nowMs - lastMouseDebug >= 500) {
        lastMouseDebug = nowMs;
        Serial.printf("[MOUSE] raw=(%ld,%ld) vel=%.1f scale=%.2f final=(%ld,%ld) rem=(%.2f,%.2f)\n",
                      (long)dx, (long)dy, velocity, combinedScale,
                      (long)finalDx, (long)finalDy, remainderX, remainderY);
    }
    
    // === UPDATE LAST POSITION (only at the end) ===
    mouseLastX = x;
    mouseLastY = y;
}

void gearvr_update_mouse()
{
    // Skip if Gear VR not connected
    if (!gearVR.connected) {
        return;
    }
    
    // NOTE: Mouse MOVEMENT is handled directly in notifyCallback() for zero latency.
    // This function only handles button state changes (polled from loop()).

    // === MOUSE BUTTONS (with Debounce) ===
    bool leftBtn = gearVR.triggerPressed;      // Trigger -> Left Click
    bool rightBtn = gearVR.backPressed;        // Back -> Right Click
    bool middleBtn = gearVR.homePressed;       // Home -> Middle Click
    
    uint32_t now = millis();
    
    // Left button (Trigger) with debounce
    if (leftBtn != mouseLastLeft) {
        if (now - lastLeftChange >= BUTTON_DEBOUNCE_MS) {
            if (leftBtn) {
                Mouse.press(MOUSE_LEFT);
            } else {
                Mouse.release(MOUSE_LEFT);
            }
            mouseLastLeft = leftBtn;
            lastLeftChange = now;
        }
    }
    
    // Right button (Back) with debounce
    if (rightBtn != mouseLastRight) {
        if (now - lastRightChange >= BUTTON_DEBOUNCE_MS) {
            if (rightBtn) {
                Mouse.press(MOUSE_RIGHT);
            } else {
                Mouse.release(MOUSE_RIGHT);
            }
            mouseLastRight = rightBtn;
            lastRightChange = now;
        }
    }
    
    // Middle button (Home) with debounce
    if (middleBtn != mouseLastMiddle) {
        if (now - lastMiddleChange >= BUTTON_DEBOUNCE_MS) {
            if (middleBtn) {
                Mouse.press(MOUSE_MIDDLE);
            } else {
                Mouse.release(MOUSE_MIDDLE);
            }
            mouseLastMiddle = middleBtn;
            lastMiddleChange = now;
        }
    }
}

void gearvr_get_mouse_delta(int16_t *dx, int16_t *dy)
{
    if (!gearVR.connected || !gearVR.touchActive) {
        *dx = 0;
        *dy = 0;
        return;
    }
    
    // Calculate delta from last position
    *dx = (int16_t)(gearVR.touchX - mouseLastX);
    *dy = (int16_t)(gearVR.touchY - mouseLastY);
}

bool gearvr_get_mouse_buttons(bool *left, bool *right, bool *middle)
{
    if (!gearVR.connected) {
        *left = false;
        *right = false;
        *middle = false;
        return false;
    }
    
    *left = gearVR.triggerPressed;
    *right = gearVR.backPressed;
    *middle = gearVR.homePressed;
    return true;
}
