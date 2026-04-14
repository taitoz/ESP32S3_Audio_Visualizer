#include "gearvr_controller.h"
#include <NimBLEDevice.h>
#include <string>

/*******************************************************************************
 * Gear VR Controller - BLE Implementation
 * 
 * Uses NimBLE stack for low-memory Bluetooth LE.
 * Connects to Gear VR controller by MAC address and subscribes to notifications.
 ******************************************************************************/

// Gear VR Controller MAC address
#define GEARVR_MAC_ADDRESS "2c:ba:ba:2a:d4:05"

// Oculus proprietary service and characteristics
static BLEUUID serviceUUID("4f63756c-7573-2054-6872-6565646f6f6d");
static BLEUUID dataCharUUID("c8c51726-81bc-483b-a052-f7a14ea3d281");  // Main data stream
static BLEUUID commandCharUUID("c8c51726-81bc-483b-a052-f7a14ea3d282");  // Command channel

// Battery service (standard BLE)
static BLEUUID batteryServiceUUID((uint16_t)0x180F);
static BLEUUID batteryLevelUUID((uint16_t)0x2A19);

volatile GearVRState gearVR = {0};

static NimBLEClient* pClient = nullptr;
static NimBLERemoteCharacteristic* pDataChar = nullptr;
static NimBLERemoteCharacteristic* pBatteryChar = nullptr;

// Previous touchpad position for delta calculation
static uint16_t lastTouchX = 0;
static uint16_t lastTouchY = 0;

// Notification callback for main data stream
static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify)
{
    if (length < 60) return;  // Gear VR sends 60-byte packets
    
    // Parse touchpad (bytes 54-57)
    gearVR.touchX = (pData[54] << 8) | pData[55];
    gearVR.touchY = (pData[56] << 8) | pData[57];
    gearVR.touchActive = (pData[58] & 0x01) != 0;
    
    // Parse buttons (byte 58)
    gearVR.touchpadClicked = (pData[58] & 0x02) != 0;
    gearVR.triggerPressed = (pData[58] & 0x04) != 0;
    gearVR.backPressed = (pData[58] & 0x08) != 0;
    gearVR.homePressed = (pData[58] & 0x10) != 0;
    gearVR.volumeUpPressed = (pData[58] & 0x20) != 0;
    gearVR.volumeDownPressed = (pData[58] & 0x40) != 0;
    
    // Parse IMU data (bytes 4-27)
    // Accelerometer (bytes 4-9, little-endian int16)
    gearVR.accelX = (int16_t)((pData[5] << 8) | pData[4]);
    gearVR.accelY = (int16_t)((pData[7] << 8) | pData[6]);
    gearVR.accelZ = (int16_t)((pData[9] << 8) | pData[8]);
    
    // Gyroscope (bytes 10-15)
    gearVR.gyroX = (int16_t)((pData[11] << 8) | pData[10]);
    gearVR.gyroY = (int16_t)((pData[13] << 8) | pData[12]);
    gearVR.gyroZ = (int16_t)((pData[15] << 8) | pData[14]);
    
    // Magnetometer (bytes 16-21)
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
    if (gearVR.connected) {
        Serial.println("⚠ Already connected to Gear VR Controller");
        return;
    }
    
    Serial.println("\n┌─────────────────────────────────────┐");
    Serial.println("│ CONNECTING TO GEAR VR CONTROLLER   │");
    Serial.println("└─────────────────────────────────────┘");
    Serial.printf("Target MAC: %s\n", GEARVR_MAC_ADDRESS);
    Serial.println("Connecting (this may take 10-15 seconds)...");
    Serial.flush();
    
    NimBLEAddress address(std::string(GEARVR_MAC_ADDRESS), BLE_ADDR_PUBLIC);
    
    // Direct connection with extended timeout
    pClient->setConnectTimeout(15);  // 15 second timeout
    
    if (!pClient->connect(address, false)) {  // false = don't delete attributes
        Serial.println("✗ FAILED: Could not connect to controller");
        Serial.println("  → Check controller is powered on");
        Serial.println("  → Press Home + Trigger for 5 sec (pairing mode)");
        Serial.println("  → Move controller closer (<5m)");
        Serial.printf("  → Verify MAC address: %s\n", GEARVR_MAC_ADDRESS);
        return;
    }
    
    Serial.println("✓ BLE connection established");
    Serial.println("Discovering services...");
    
    // Get Oculus service
    NimBLERemoteService* pService = pClient->getService(serviceUUID);
    if (pService == nullptr) {
        Serial.println("✗ FAILED: Oculus service not found!");
        Serial.println("  → Wrong device or incompatible firmware");
        pClient->disconnect();
        return;
    }
    Serial.println("✓ Oculus service found");
    
    // Get data characteristic
    pDataChar = pService->getCharacteristic(dataCharUUID);
    if (pDataChar == nullptr) {
        Serial.println("✗ FAILED: Data characteristic not found!");
        pClient->disconnect();
        return;
    }
    Serial.println("✓ Data characteristic found");
    
    // Subscribe to notifications
    if (pDataChar->canNotify()) {
        pDataChar->subscribe(true, notifyCallback);
        Serial.println("✓ Subscribed to data stream (60-byte packets)");
    }
    
    // Get battery service
    NimBLERemoteService* pBatteryService = pClient->getService(batteryServiceUUID);
    if (pBatteryService != nullptr) {
        pBatteryChar = pBatteryService->getCharacteristic(batteryLevelUUID);
        if (pBatteryChar != nullptr && pBatteryChar->canRead()) {
            std::string value = pBatteryChar->readValue();
            if (value.length() > 0) {
                gearVR.batteryLevel = (uint8_t)value[0];
                Serial.printf("✓ Battery level: %d%%\n", gearVR.batteryLevel);
            }
        }
    }
    
    // Send initialization command (enable sensor streaming)
    NimBLERemoteCharacteristic* pCmdChar = pService->getCharacteristic(commandCharUUID);
    if (pCmdChar != nullptr && pCmdChar->canWrite()) {
        uint8_t cmd[] = {0x01, 0x00};  // Enable sensors
        pCmdChar->writeValue(cmd, sizeof(cmd), false);
        Serial.println("✓ Sent enable sensors command");
    }
    
    gearVR.connected = true;
    Serial.println("✓ Gear VR Controller ready!");
    Serial.println("  → Touchpad, buttons, and IMU active");
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
    // Update battery level periodically (every 60 seconds)
    static uint32_t lastBatteryCheck = 0;
    if (gearVR.connected && pBatteryChar != nullptr && 
        (millis() - lastBatteryCheck > 60000)) {
        if (pBatteryChar->canRead()) {
            std::string value = pBatteryChar->readValue();
            if (value.length() > 0) {
                gearVR.batteryLevel = (uint8_t)value[0];
            }
        }
        lastBatteryCheck = millis();
    }
    
    // Check connection timeout (no data for 5 seconds = disconnected)
    if (gearVR.connected && (millis() - gearVR.lastUpdateMs > 5000)) {
        Serial.println("Gear VR Controller timeout, reconnecting...");
        gearvr_disconnect();
        delay(1000);
        gearvr_connect();
    }
}

void gearvr_get_mouse_delta(int16_t *dx, int16_t *dy)
{
    if (!gearVR.connected || !gearVR.touchActive) {
        *dx = 0;
        *dy = 0;
        lastTouchX = gearVR.touchX;
        lastTouchY = gearVR.touchY;
        return;
    }
    
    // Calculate delta from last position
    *dx = (int16_t)(gearVR.touchX - lastTouchX);
    *dy = (int16_t)(gearVR.touchY - lastTouchY);
    
    // Apply sensitivity scaling (adjust as needed)
    *dx = (*dx * 2) / 3;  // Reduce sensitivity slightly
    *dy = (*dy * 2) / 3;
    
    // Update last position
    lastTouchX = gearVR.touchX;
    lastTouchY = gearVR.touchY;
}
