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
static float filteredDX = 0.0f;
static float filteredDY = 0.0f;

// Helper function to reset mouse state
static void resetMouseState()
{
    mouseLastX = 0;
    mouseLastY = 0;
    mouseLastLeft = false;
    mouseLastRight = false;
    mouseLastMiddle = false;
    filteredDX = 0.0f;
    filteredDY = 0.0f;
}

// Notification callback for main data stream
static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify)
{
    if (length < 60) {
        Serial.printf("[BLE] Packet too short: %d bytes\n", length);
        return;
    }
    
    // === CORRECT 10-BIT PARSING ALGORITHM ===
    
    // Touch Active flag (byte 54, bit 4)
    bool touchActive = (pData[54] & 0x10) > 0;
    
    // X coordinate (10-bit): 4 bits from byte 55 + 6 bits from byte 56
    uint16_t rawX = ((pData[55] & 0x0F) << 6) | (pData[56] >> 2);
    
    // Y coordinate (10-bit): 2 bits from byte 56 + 8 bits from byte 57
    uint16_t rawY = ((pData[56] & 0x03) << 8) | pData[57];
    
    // Trigger (byte 58, bit 0)
    bool trigger = (pData[58] & 0x01) > 0;
    
    // Buttons from byte 54
    bool homeBtn = (pData[54] & 0x01) > 0;
    bool backBtn = (pData[54] & 0x02) > 0;
    bool volDown = (pData[54] & 0x04) > 0;
    bool volUp = (pData[54] & 0x08) > 0;
    
    // Battery (byte 59)
    uint8_t battery = pData[59];
    
    // === UPDATE GLOBAL STATE ===
    gearVR.touchActive = touchActive;
    gearVR.triggerPressed = trigger;
    gearVR.homePressed = homeBtn;
    gearVR.backPressed = backBtn;
    gearVR.volumeDownPressed = volDown;
    gearVR.volumeUpPressed = volUp;
    gearVR.batteryLevel = battery;
    
    // Update coordinates (keep last value if not touching)
    if (touchActive) {
        gearVR.touchX = rawX;
        gearVR.touchY = rawY;
    }
    
    // === OUTPUT ONLY ON CHANGES ===
    static bool lastTouchActive = false;
    static bool lastTrigger = false;
    static uint16_t lastX = 0;
    static uint16_t lastY = 0;
    
    bool changed = (touchActive != lastTouchActive) || 
                   (trigger != lastTrigger) ||
                   (touchActive && (rawX != lastX || rawY != lastY));
    
    if (changed) {
        if (touchActive) {
            Serial.printf("[OK] TOUCH: X=%d Y=%d | TRIG=%d | Buttons: H=%d B=%d V+=%d V-=%d\n", 
                          rawX, rawY, trigger, homeBtn, backBtn, volUp, volDown);
        } else if (trigger) {
            Serial.printf("[OK] TRIGGER ONLY | Last X=%d Y=%d\n", gearVR.touchX, gearVR.touchY);
        } else if (lastTouchActive) {
            Serial.printf("[OK] TOUCH RELEASED | Last X=%d Y=%d\n", gearVR.touchX, gearVR.touchY);
        }
        
        lastTouchActive = touchActive;
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
    return gearVR.connected && pClient && pClient->isConnected();Исправь инициализацию USB. Убедись, что используется USB.begin() вместе с Mouse.begin(). Весь отладочный вывод должен идти через Serial, который в режиме S3 OTG привязан к нативному USB. Добавь проверку: если USB не подключен, код не должен блокироваться, чтобы BLE продолжал работать
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

// Configuration
#define MOUSE_DEADZONE 2        // Ignore movements < 2 units (reduce drift)
#define MOUSE_MAX_STEP 50       // Max pixels per update (prevent cursor "flying")
#define MOUSE_SENSITIVITY 1.2   // Sensitivity multiplier
#define MOUSE_SMOOTHING 0.3f    // Alpha coefficient (0.1=very smooth, 1.0=no smoothing)

void gearvr_update_mouse()
{
    // Skip if Gear VR not connected
    if (!gearVR.connected) {
        return;
    }
    
    // Note: USB HID Mouse operations are non-blocking on ESP32-S3
    // If USB is not connected, Mouse.move() and Mouse.press/release() are no-ops
    
    // === MOUSE MOVEMENT (Relative with Exponential Smoothing) ===
    
    if (gearVR.touchActive) {
        // Calculate raw delta
        int16_t rawDx = (int16_t)(gearVR.touchX - mouseLastX);
        int16_t rawDy = (int16_t)(gearVR.touchY - mouseLastY);
        
        // Update last position immediately (for next delta calculation)
        mouseLastX = gearVR.touchX;
        mouseLastY = gearVR.touchY;
        
        // Apply deadzone filter (eliminate drift when finger is stationary)
        if (abs(rawDx) < MOUSE_DEADZONE) {
            rawDx = 0;
        }
        if (abs(rawDy) < MOUSE_DEADZONE) {
            rawDy = 0;
        }
        
        // Skip if no movement
        if (rawDx == 0 && rawDy == 0) {
            return;
        }
        
        // Apply sensitivity
        float sensitiveDx = rawDx * MOUSE_SENSITIVITY;
        float sensitiveDy = rawDy * MOUSE_SENSITIVITY;
        
        // Apply exponential moving average filter (smooth movement)
        // Formula: filtered = (new * alpha) + (old * (1 - alpha))
        filteredDX = (sensitiveDx * MOUSE_SMOOTHING) + (filteredDX * (1.0f - MOUSE_SMOOTHING));
        filteredDY = (sensitiveDy * MOUSE_SMOOTHING) + (filteredDY * (1.0f - MOUSE_SMOOTHING));
        
        // Convert to integer for Mouse.move()
        int16_t finalDx = (int16_t)filteredDX;
        int16_t finalDy = (int16_t)filteredDY;
        
        // Clamp to max step (prevent cursor flying on sudden movements)
        if (finalDx > MOUSE_MAX_STEP) finalDx = MOUSE_MAX_STEP;
        if (finalDx < -MOUSE_MAX_STEP) finalDx = -MOUSE_MAX_STEP;
        if (finalDy > MOUSE_MAX_STEP) finalDy = MOUSE_MAX_STEP;
        if (finalDy < -MOUSE_MAX_STEP) finalDy = -MOUSE_MAX_STEP;
        
        // Send movement if non-zero
        if (finalDx != 0 || finalDy != 0) {
            Mouse.move(finalDx, finalDy);
        }
    } else {
        // Touch released - reset tracking and filter
        mouseLastX = gearVR.touchX;
        mouseLastY = gearVR.touchY;
        filteredDX = 0.0f;
        filteredDY = 0.0f;
    }
    
    // === MOUSE BUTTONS ===
    bool leftBtn = gearVR.triggerPressed;      // Trigger -> Left Click
    bool rightBtn = gearVR.backPressed;        // Back -> Right Click
    bool middleBtn = gearVR.homePressed;       // Home -> Middle Click
    
    // Update buttons only on state change
    if (leftBtn != mouseLastLeft) {
        if (leftBtn) {
            Mouse.press(MOUSE_LEFT);
        } else {
            Mouse.release(MOUSE_LEFT);
        }
        mouseLastLeft = leftBtn;
    }
    
    if (rightBtn != mouseLastRight) {
        if (rightBtn) {
            Mouse.press(MOUSE_RIGHT);
        } else {
            Mouse.release(MOUSE_RIGHT);
        }
        mouseLastRight = rightBtn;
    }
    
    if (middleBtn != mouseLastMiddle) {
        if (middleBtn) {
            Mouse.press(MOUSE_MIDDLE);
        } else {
            Mouse.release(MOUSE_MIDDLE);
        }
        mouseLastMiddle = middleBtn;
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
