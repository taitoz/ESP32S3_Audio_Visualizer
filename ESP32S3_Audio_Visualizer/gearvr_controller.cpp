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
        Serial.printf("[BLE] Packet too short: %d bytes\n", length);
        return;
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
    
    // === OUTPUT ONLY ON CHANGES ===
    static bool lastTouchActive = false;
    static bool lastTrigger = false;
    static uint16_t lastX = 0;
    static uint16_t lastY = 0;
    
    bool changed = (fingerOnPad != lastTouchActive) || 
                   (trigger != lastTrigger) ||
                   (fingerOnPad && (rawX != lastX || rawY != lastY));
    
    if (changed) {
        if (fingerOnPad) {
            Serial.printf("[OK] TOUCH: X=%d Y=%d | TRIG=%d | Click=%d | Buttons: H=%d B=%d V+=%d V-=%d\n", 
                          rawX, rawY, trigger, touchpadBtn, homeBtn, backBtn, volUp, volDown);
        } else if (trigger) {
            Serial.printf("[OK] TRIGGER ONLY | Last X=%d Y=%d\n", gearVR.touchX, gearVR.touchY);
        } else if (lastTouchActive) {
            Serial.printf("[OK] TOUCH RELEASED | Last X=%d Y=%d\n", gearVR.touchX, gearVR.touchY);
        }
        
        lastTouchActive = fingerOnPad;
        lastTrigger = trigger;
        lastX = rawX;
        lastY = rawY;
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
    
    gearVR.lastUpdateMs = millis();
}

// Client callbacks
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║  GEAR VR CONTROLLER CONNECTED! ✓      ║");
        Serial.println("╚════════════════════════════════════════╝");
        Serial.printf("MAC: %s\n", GEARVR_MAC_ADDRESS);
        Serial.printf("RSSI: %d dBm\n", pClient->getRssi());
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
    pClient->setConnectionParams(12, 12, 0, 51);  // Low latency
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

// Professional Trackpad Configuration (Ballistics)
#define MOUSE_DEADZONE 3        // Ignore movements < 3 units (ADC noise filter)
#define MOUSE_BASE_SENS 0.5f    // Base sensitivity for slow movements (increased from 0.15)
#define MOUSE_ACCEL_FACTOR 0.01f   // Velocity-based acceleration factor (increased from 0.005)
#define MOUSE_MAX_STEP 150      // Max pixels per update (allow fast swipes)
#define MOUSE_INVERT_X false    // Don't invert X axis
#define MOUSE_INVERT_Y false    // Don't invert Y axis (was inverted incorrectly)

void gearvr_update_mouse()
{
    // Skip if Gear VR not connected
    if (!gearVR.connected) {
        return;
    }
    
    // Note: USB HID Mouse operations are non-blocking on ESP32-S3
    // If USB is not connected, Mouse.move() and Mouse.press/release() are no-ops
    
    // === PROFESSIONAL TRACKPAD LOGIC (Ballistics + Anti-Jitter) ===
    
    if (gearVR.touchActive) {
        // First touch frame - just record position, don't move cursor (prevent jump)
        if (!wasTouched) {
            mouseLastX = gearVR.touchX;
            mouseLastY = gearVR.touchY;
            wasTouched = true;
            goto handle_buttons;  // Skip movement on first touch
        }
        
        // Calculate raw delta from last position
        int16_t dx = (int16_t)(gearVR.touchX - mouseLastX);
        int16_t dy = (int16_t)(gearVR.touchY - mouseLastY);
        
        // Update last position for next frame
        mouseLastX = gearVR.touchX;
        mouseLastY = gearVR.touchY;
        
        // Apply deadzone (filter ADC noise)
        if (abs(dx) < MOUSE_DEADZONE) dx = 0;
        if (abs(dy) < MOUSE_DEADZONE) dy = 0;
        
        // Skip if no movement
        if (dx == 0 && dy == 0) {
            goto handle_buttons;
        }
        
        // Apply axis inversion
        if (MOUSE_INVERT_X) dx = -dx;
        if (MOUSE_INVERT_Y) dy = -dy;
        
        // Calculate velocity for ballistics (dynamic acceleration)
        float velocity = sqrt((float)(dx * dx + dy * dy));
        
        // Ballistics: sensitivity increases with velocity
        // factor = baseSens + (velocity * accelFactor)
        // Slow movement: ~0.15x, Fast movement: up to 1.0x+
        float factor = MOUSE_BASE_SENS + (velocity * MOUSE_ACCEL_FACTOR);
        
        // Apply sensitivity with ballistics
        float moveX = (float)dx * factor;
        float moveY = (float)dy * factor;
        
        // Add to remainder (accumulate fractional part)
        remainderX += moveX;
        remainderY += moveY;
        
        // Extract integer part for Mouse.move()
        int16_t finalDx = (int16_t)remainderX;
        int16_t finalDy = (int16_t)remainderY;
        
        // Keep fractional part in remainder (anti-jitter)
        remainderX -= (float)finalDx;
        remainderY -= (float)finalDy;
        
        // Clamp to max step (prevent cursor flying)
        if (finalDx > MOUSE_MAX_STEP) {
            finalDx = MOUSE_MAX_STEP;
            remainderX = 0.0f;
        }
        if (finalDx < -MOUSE_MAX_STEP) {
            finalDx = -MOUSE_MAX_STEP;
            remainderX = 0.0f;
        }
        if (finalDy > MOUSE_MAX_STEP) {
            finalDy = MOUSE_MAX_STEP;
            remainderY = 0.0f;
        }
        if (finalDy < -MOUSE_MAX_STEP) {
            finalDy = -MOUSE_MAX_STEP;
            remainderY = 0.0f;
        }
        
        // Send movement to USB HID
        if (finalDx != 0 || finalDy != 0) {
            Mouse.move(finalDx, finalDy);
        }
    } else {
        // Touch released - reset state
        wasTouched = false;
        remainderX = 0.0f;
        remainderY = 0.0f;
    }
    
handle_buttons:
    
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
