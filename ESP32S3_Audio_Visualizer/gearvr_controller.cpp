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
#define HID_CC_MUTE              0x00E2
#define HID_CC_PLAY_PAUSE        0x00CD
#define HID_CC_SCAN_NEXT_TRACK   0x00B5   // MEDIA_NEXT
#define HID_CC_SCAN_PREV_TRACK   0x00B6   // MEDIA_PREVIOUS
#define HID_CC_AC_BACK           0x0224
#define HID_CC_AC_FORWARD        0x0225

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
// Motion-gate for bias calibration: any raw gyro sample above this magnitude
// is treated as "controller is moving" and disqualifies the surrounding
// calibration window. Typical rest noise is <15 units, typical motion is >200.
#define GYRO_BIAS_MOTION_GATE     150

// Accelerometer bias (captured during SAME warmup window as gyro bias).
// CRITICAL: without this, the non-zero gravity component on the "horizontal"
// accel axes (e.g. ~447 on Y when held flat) feeds directly into the fusion
// as a constant pull, causing the cursor to drift and fight upward motion.
static float accelBiasX = 0.0f;
static float accelBiasY = 0.0f;
static float accelBiasZ = 0.0f;

// Minimal EMA (alpha = 0.9 — kills single-sample sensor noise only)
static float emaDeltaX = 0.0f;
static float emaDeltaY = 0.0f;

// Low-pass-filtered accelerometer for fusion input. The raw accel signal has
// sharp per-sample noise that, mixed into the fusion output, manifests as the
// axis-aligned "diamond grid" on slow movement. Heavy LPF (alpha=0.2) keeps
// only the slow tilt trend, which is all the fusion term should contribute.
static float filteredAccelX = 0.0f;
static float filteredAccelY = 0.0f;
static bool  accelFilterPrimed = false;

// Sub-pixel float accumulators (prevent int-truncation "grid" jitter)
static float remainderX = 0.0f;
static float remainderY = 0.0f;

// Bias drift is handled by (a) one-shot warmup at connect and (b) an
// accel-gated stillness EMA. No per-touch recalibration — gyro is an
// ANGULAR-RATE sensor (0 at rest regardless of orientation), so there is
// no "zero orientation" to redefine per touch.

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
#define ACCEL_STILL_THRESHOLD  20   // |Δaccel| below this = hand is truly still → update bias

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
static bool lastHome = false;
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
    if (lastBack || lastHome || lastVolUp || lastVolDown) {
        ConsumerControl.release();
        lastBack = lastHome = lastVolUp = lastVolDown = false;
    }
    wasTouched = false;
    scrollInit = false;
    emaDeltaX = 0.0f;
    emaDeltaY = 0.0f;
    filteredAccelX = filteredAccelY = 0.0f;
    accelFilterPrimed = false;
    scrollRemainder  = 0.0f;
    scrollRemainderX = 0.0f;
    remainderX = remainderY = 0.0f;
    scrollLockUntilMs = 0;
    accelPrimed = false;
    prevAccelX = prevAccelY = prevAccelZ = 0;
    gyroBiasPrimed = false;   // re-prime bias on next connect
    gyroBiasCount = 0;
    gyroBiasX = gyroBiasY = gyroBiasZ = 0.0f;
    accelBiasX = accelBiasY = accelBiasZ = 0.0f;
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
    // Phase 1 (warmup): accumulate running average of gyro samples while the
    //          controller is physically still. Cursor is FROZEN during this
    //          window (handleAirMouse checks gyroBiasPrimed).
    // Phase 2 (run): slow EMA tracker that only updates when controller is still.
    //
    // CRITICAL: warmup must NOT capture samples while the controller is moving
    // (user activating it, hand still in motion, etc.). A single big sample
    // during warmup permanently corrupts the bias — e.g. gyroZ=-146 observed
    // in the field made the cursor fly sideways at rest regardless of user
    // input. Fix: if a sample's magnitude exceeds the gate, RESET the
    // accumulator and start over. Calibration only completes when the
    // controller has been still for GYRO_BIAS_WARMUP_SAMPLES consecutive frames.
    if (!gyroBiasPrimed) {
        // Reject any sample where ANY axis shows motion-level magnitude.
        // Restart the window from zero so a clean run of N still frames is required.
        if (abs(gX) > GYRO_BIAS_MOTION_GATE ||
            abs(gY) > GYRO_BIAS_MOTION_GATE ||
            abs(gZ) > GYRO_BIAS_MOTION_GATE) {
            if (gyroBiasCount > 0) {
                Serial.printf("[GYRO] Bias warmup aborted (motion: gyr=%d,%d,%d) — restarting\n",
                              gX, gY, gZ);
            }
            gyroBiasCount = 0;
            gyroBiasX = gyroBiasY = gyroBiasZ = 0.0f;
            accelBiasX = accelBiasY = accelBiasZ = 0.0f;
        } else {
            // Incremental running average: bias_n = bias_(n-1) + (sample - bias_(n-1)) / n
            gyroBiasCount++;
            float invN = 1.0f / (float)gyroBiasCount;
            gyroBiasX += ((float)gX - gyroBiasX) * invN;
            gyroBiasY += ((float)gY - gyroBiasY) * invN;
            gyroBiasZ += ((float)gZ - gyroBiasZ) * invN;
            // Accelerometer bias captured in the SAME window — this becomes the
            // "zero orientation" reference for the fusion term. Any later tilt
            // contributes a non-zero accel delta that nudges the cursor; at rest
            // the cursor receives zero accel contribution (no drift).
            accelBiasX += ((float)aX - accelBiasX) * invN;
            accelBiasY += ((float)aY - accelBiasY) * invN;
            accelBiasZ += ((float)aZ - accelBiasZ) * invN;
            if (gyroBiasCount >= GYRO_BIAS_WARMUP_SAMPLES) {
                gyroBiasPrimed = true;
                Serial.printf("[GYRO]  Bias: X=%.1f Y=%.1f Z=%.1f\n",
                              gyroBiasX, gyroBiasY, gyroBiasZ);
                Serial.printf("[ACCEL] Bias: X=%.1f Y=%.1f Z=%.1f (zero-orientation ref)\n",
                              accelBiasX, accelBiasY, accelBiasZ);
            }
        }
    }
    // === STILLNESS CALIBRATION (accelerometer-gated drift tracker) ===
    // Only update gyro bias when the ACCELEROMETER confirms the controller
    // is physically still (|Δaccel| < threshold on all axes). Unlike the
    // earlier gyro-only drift tracker, this CANNOT absorb sustained rotation
    // because rotation always shows up on accel too. And unlike the earlier
    // "still-detect", this NEVER force-zeroes the gyro output — it only
    // nudges bias slowly → no directional asymmetry.
    else if (accelPrimed) {
        int32_t dAX = (int32_t)aX - (int32_t)prevAccelX;
        int32_t dAY = (int32_t)aY - (int32_t)prevAccelY;
        int32_t dAZ = (int32_t)aZ - (int32_t)prevAccelZ;
        if (abs(dAX) < ACCEL_STILL_THRESHOLD &&
            abs(dAY) < ACCEL_STILL_THRESHOLD &&
            abs(dAZ) < ACCEL_STILL_THRESHOLD) {
            const float STILLNESS_ALPHA = 0.001f;   // very slow — adapts over seconds
            gyroBiasX += STILLNESS_ALPHA * ((float)gX - gyroBiasX);
            gyroBiasY += STILLNESS_ALPHA * ((float)gY - gyroBiasY);
            gyroBiasZ += STILLNESS_ALPHA * ((float)gZ - gyroBiasZ);
            // Track accel bias (zero-orientation reference) the SAME way.
            // Without this, a single warmup-captured bias goes stale the
            // moment the user changes the controller's rest orientation,
            // and fusion starts pulling the cursor constantly. Slow EMA
            // during stillness lets the reference follow the current
            // resting orientation over a few seconds of stillness.
            accelBiasX += STILLNESS_ALPHA * ((float)aX - accelBiasX);
            accelBiasY += STILLNESS_ALPHA * ((float)aY - accelBiasY);
            accelBiasZ += STILLNESS_ALPHA * ((float)aZ - accelBiasZ);
        }
    }
    prevAccelX = aX;
    prevAccelY = aY;
    prevAccelZ = aZ;
    accelPrimed = true;
    
    // Reset EMA history on touch-begin so stale leftovers from a previous
    // gesture don't bleed into the first few frames of the new one.
    if (fingerOnPad && !wasTouched) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        // remainderX/Y intentionally NOT reset (persist sub-pixel fractions)
    }
    
    // === IMMEDIATE SCROLL + AIR MOUSE (order matters!) ===
    // handleScroll runs first: it may set scrollLockUntilMs if finger is
    // moving fast, then handleAirMouse will honor that lock in the same frame.
    //
    // Air-mouse activation: the user HOLDS THE TRIGGER to point. Trigger is
    // a physical button with a firm pull, so the hand is steady while active.
    // Touchpad click is reserved for LEFT mouse click.
    handleScroll(rawX, rawY, fingerOnPad);
    handleAirMouse(trigger);
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
        // Extend link-layer PDU to 251 bytes — lets one 60-byte IMU packet fit
        // in a single LL frame (default is 27). MTU itself is preferred
        // globally in gearvr_init() via NimBLEDevice::setMTU().
        pClient->setDataLen(251);
        Serial.println("[BLE] Conn params set (7.5-11.25ms, 4s timeout), LL PDU=251");
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
    NimBLEDevice::setMTU(517);               // ATT spec max — avoid packet fragmentation
    
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
    
    // === KEEP-ALIVE 2.0 (anti-disconnect) ===
    // (a) Command write every 5 s — nudges the command channel.
    // (b) Battery-Level READ every 10 s — hits a DIFFERENT characteristic
    //     on a DIFFERENT service. Reads force an ATT round-trip with a
    //     response, which reliably yanks the controller's BLE stack out of
    //     low-power mode when command-channel writes alone aren't enough.
    if (millis() - lastKeepAlive > 5000) {
        if (pCommandChar != nullptr && pCommandChar->canWriteNoResponse()) {
            uint8_t keepAlive[] = {0x01, 0x00, 0x00};
            pCommandChar->writeValue(keepAlive, sizeof(keepAlive), false);
        }
        lastKeepAlive = millis();
    }
    
    static uint32_t lastBatteryPing = 0;
    if (millis() - lastBatteryPing > 10000) {
        if (pBatteryChar != nullptr && pBatteryChar->canRead()) {
            std::string v = pBatteryChar->readValue();
            if (v.length() > 0) {
                gearVR.batteryLevel = (uint8_t)v[0];
            }
        }
        lastBatteryPing = millis();
    }
    
    // === WAKE ATTEMPT: No data for 4 s → re-send activation command ===
    // Gear VR controller suspends BLE notifications after short idle periods.
    // The 1 Hz keep-alive alone doesn't wake it — we need to re-issue the
    // same activation sequence used at connect. Retry every 2 s. We try
    // WITH response first (most reliable wake); fall back to no-response
    // write if the characteristic doesn't advertise the WRITE property.
    static uint32_t lastWakeAttempt = 0;
    if (gearVR.connected &&
        (millis() - gearVR.lastUpdateMs > 4000) &&
        (millis() - lastWakeAttempt > 2000)) {
        lastWakeAttempt = millis();
        uint32_t idleS = (millis() - gearVR.lastUpdateMs) / 1000;
        if (pCommandChar == nullptr) {
            Serial.printf("[BLE] 💤 No data %lus — cannot wake: pCommandChar=null\n", idleS);
        } else {
            uint8_t wake[] = {0x01, 0x00};
            bool ok = false;
            if (pCommandChar->canWrite()) {
                ok = pCommandChar->writeValue(wake, sizeof(wake), true);
                Serial.printf("[BLE] 💤 No data %lus — wake (w/response) %s\n",
                              idleS, ok ? "sent ✓" : "FAILED ✗");
            } else if (pCommandChar->canWriteNoResponse()) {
                ok = pCommandChar->writeValue(wake, sizeof(wake), false);
                Serial.printf("[BLE] 💤 No data %lus — wake (no-response) %s\n",
                              idleS, ok ? "sent ✓" : "FAILED ✗");
            } else {
                Serial.printf("[BLE] 💤 No data %lus — cannot wake: char not writable\n", idleS);
            }
        }
    }
    
    // === TIMEOUT CHECK: No data for 60 s = disconnect ===
    // Raised from 30 s — user reported reconnect after ~1 minute in a real
    // session. Give the wake attempts more chances before tearing down.
    if (gearVR.connected && (millis() - gearVR.lastUpdateMs > 60000)) {
        Serial.println("[BLE] ⏱️  Data timeout (60s), disconnecting...");
        gearvr_disconnect();
        lastConnectAttempt = millis();
    }
}

/*******************************************************************************
 * USB HID Integration — Air Mouse + Touchpad Scroll + Consumer Control
 *
 * Input mapping (current):
 *   • Cursor          : Gyroscope angular rate (yaw=X, pitch=Y). Active
 *                       ONLY while the Trigger is held.
 *   • Scroll wheel    : Touchpad vertical delta (ΔY while finger on pad)
 *   • Horizontal pan  : Touchpad horizontal delta (ΔX while finger on pad)
 *   • Trigger             → Air-mouse gate (hold to point)
 *   • Touchpad click L-zone (x ≤ 160) → Mouse LEFT click
 *   • Touchpad click R-zone (x >  160) → Mouse RIGHT click
 *   • Home button         → Consumer MUTE
 *   • Back button         → Consumer PLAY_PAUSE
 *   • Volume +            → Consumer Volume Increment
 *   • Volume −            → Consumer Volume Decrement
 ******************************************************************************/

// Air Mouse configuration — GYROSCOPE angular-rate pointing
// Cursor velocity = gyro rate × sensitivity. No acceleration curve, no soft zone —
// gyro reads 0 at rest so deadzone can be minimal (just noise floor).
#define AIR_GYRO_DEADZONE    10        // Lowered from 25: activation now requires a firm
                                       // touchpad CLICK, so the user's grip is steady while
                                       // the air mouse is active → tremor budget can be small.
                                       // Smaller deadzone gives fine precision on 5–10 px
                                       // micro-adjustments. Residual bias is removed by
                                       // warmup + stillness tracker, not by a wide deadzone.
#define AIR_GYRO_SENS        0.08f     // Slightly lower than 0.10 — with the smaller deadzone
                                       // the effective low-end gain actually INCREASES, so we
                                       // pull the nominal back a touch to keep mid-range feel.
#define AIR_GYRO_ACCEL_REF   700.0f    // Between 600 and 800 — middle ground for quadratic onset.
#define AIR_GYRO_ACCEL       0.9f      // Slight reduction so that low-speed micro-moves stay
                                       // LINEAR (predictable for 10-px precision) and only
                                       // real fast sweeps get boosted.
#define AIR_GYRO_MAX         4000      // Clamp extreme spikes (sensor glitch protection)
#define AIR_EMA_ALPHA        0.40f     // Raised from 0.30: while touchpad-clicked (firm grip),
                                       // hand tremor is much smaller, so we don't need as much
                                       // smoothing — more responsiveness is preferable for
                                       // precision pointing. Each frame contributes 40%, so
                                       // the filter still removes single-sample noise spikes.

// === LINEAR FUSION (Oculus-style: 70% gyro + 30% accel tilt) ===
// Gyro alone is aggressive and shows axis-aligned "diamond" artefacts on
// diagonals. Blending in accel-derived tilt biases the cursor toward the
// TRUE motion vector (accelerometer is immune to rotational bias), which
// straightens diagonals and stops sudden corner-snaps on BLE stalls.
//   targetX = gyroZ * W_GYRO + accel_tiltX * ACCEL_SCALE * W_ACCEL
//   targetY = gyroY * W_GYRO + accel_tiltY * ACCEL_SCALE * W_ACCEL
//   ACCEL_SCALE brings raw accel (~±16000 @ 1g) into gyro-rate units (~±500)
// NOTE: fusion disabled by default — warmup accel bias is captured once in
// whatever orientation the controller happens to be in. If the user later
// rests it in a different orientation, the (accel - bias) delta becomes a
// constant DC pull that drifts the cursor diagonally. Pure gyro has no
// such issue (gyro reads 0 at rest regardless of orientation).
// Re-enable ONLY after the stillness-tracked accel bias (below) is proven
// to continuously re-calibrate the zero-orientation reference.
#define AIR_FUSION_ENABLE       0         // 0 = pure gyro, 1 = linear fusion
#define AIR_FUSION_W_GYRO       0.80f    // gyro dominant → keeps movement "light"
#define AIR_FUSION_W_ACCEL      0.20f    // accel only corrects slow drift / diagonals
#define AIR_FUSION_ACCEL_LPF    0.20f    // per-sample LPF for accel (0.2 = heavy smoothing)
#define AIR_FUSION_ACCEL_SCALE  0.04f     // brings raw accel (±16k) into gyro scale
// Per-axis SIGN for accel contribution — gyro and accel often report OPPOSITE
// signs for the same physical motion (e.g. pitch-up increases accelY but
// gyroY is negative). If fusion "fights" gyro on one axis (hard to move in
// one direction), flip that axis's sign.
#define AIR_FUSION_ACCEL_X_SIGN  (+1.0f)
#define AIR_FUSION_ACCEL_Y_SIGN  (-1.0f)  // flipped to match gyro pitch direction
// Axis mapping: gyroZ (yaw) → mouse X ;  gyroX (pitch) → mouse Y
// NOTE: Gear VR gyro orientation puts PITCH on the X-axis, not Y. Confirmed
// empirically in the debug log: when user tilts the controller (accelY rises
// from 1300 → 1800), gyroX shows 80–140 while gyroY stays near 0. Previously
// mouse-Y was mapped to gyroY → vertical motion felt "heavy" / nearly dead.
// If cursor flies UP when you tilt DOWN (or right when you turn left) → flip the corresponding INVERT.
// Symptom guide:
//   "Cursor flies up at rest / when controller is horizontal"           → set INVERT_Y = true
//   "Cursor moves opposite of expected horizontally (left/right swap)"  → toggle INVERT_X
#define AIR_MOUSE_INVERT_X   true      // Flip if horizontal axis feels wrong
#define AIR_MOUSE_INVERT_Y   true      // Flip if vertical axis feels wrong
#define MOUSE_HID_MAX        127       // int8_t max per Mouse.move() call

// Scroll configuration
#define SCROLL_SENS          0.05f     // Touchpad Y delta → scroll units
#define SCROLL_DEADZONE      3         // Ignore sub-pixel touchpad noise
#define SCROLL_WRAP_THRESHOLD 500      // Reject coordinate glitches

// Send mouse movement with int8_t overflow protection (split large deltas).
// === HARD CLAMP per call ===
// Sensor spikes (controller slammed / re-init) can drive dx/dy into the
// thousands. Without a clamp this fires dozens-to-hundreds of 127-px HID
// reports in ONE call → saturates the USB HID buffer pool → future
// Mouse.press()/release() calls for CLICKS silently fail to enqueue
// ("Failed to allocate buffer, retrying"). Result: clicks are logged
// but never reach the host. Cap the total move to a sane upper bound
// so a single spike emits at most ceil(200/127)=2 HID reports.
#define MOUSE_MOVE_CLAMP  200
static void sendMouseMove(int32_t dx, int32_t dy)
{
    if (dx >  MOUSE_MOVE_CLAMP) dx =  MOUSE_MOVE_CLAMP;
    if (dx < -MOUSE_MOVE_CLAMP) dx = -MOUSE_MOVE_CLAMP;
    if (dy >  MOUSE_MOVE_CLAMP) dy =  MOUSE_MOVE_CLAMP;
    if (dy < -MOUSE_MOVE_CLAMP) dy = -MOUSE_MOVE_CLAMP;
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
    // === INSTANT BRAKE: BLE disconnect safety ===
    // If the BLE link is dead or the controller struct went stale, zero EVERY
    // accumulator immediately. Without this, a disconnect mid-motion would
    // leave non-zero remainders that fire as ghost movement on reconnect.
    if (!gearVR.connected || !pClient || !pClient->isConnected()) {
        wasTouched = false;
        emaDeltaX = emaDeltaY = 0.0f;
        remainderX = remainderY = 0.0f;
        filteredAccelX = filteredAccelY = 0.0f;
        accelFilterPrimed = false;
        return;
    }
    
    // Freeze cursor while bias is still being calibrated (first ~1 s after connect).
    // Without this, an uncalibrated bias would send the cursor off-screen instantly.
    if (!gyroBiasPrimed) {
        wasTouched = touched;
        emaDeltaX = emaDeltaY = 0.0f;
        remainderX = remainderY = 0.0f;   // keep accumulators clean during warmup
        return;
    }
    
    if (!touched) {
        // === INSTANT BRAKE: finger released / active == 0 ===
        // Force-zero EVERY accumulator EVERY FRAME while idle so residual
        // sub-pixel motion cannot "fly away" the cursor on reconnect or after
        // a BLE hiccup. Also reset the accel LPF so the next touch starts
        // from a fresh filter state (no stale tilt memory).
        wasTouched = false;
        emaDeltaX = emaDeltaY = 0.0f;
        remainderX = remainderY = 0.0f;          // per-frame reset while idle
        filteredAccelX = filteredAccelY = 0.0f;
        accelFilterPrimed = false;
        return;
    }
    // === 150 ms settle after first touch ===
    // The moment a finger lands, BOTH accel and gyro spike hard (hand jolt).
    // Feeding those spikes into the fusion produces the worst "diamond" burst
    // and a visible cursor flick. Freeze cursor for 150 ms, keep accumulators
    // clean, and let the LPF prime on real signal.
    static uint32_t touchStartMs = 0;
    if (!wasTouched) {
        wasTouched = true;
        touchStartMs = millis();
        emaDeltaX = emaDeltaY = 0.0f;
        remainderX = remainderY = 0.0f;
        filteredAccelX = (float)gearVR.accelX;    // seed LPF at current sample
        filteredAccelY = (float)gearVR.accelY;
        accelFilterPrimed = true;
        return;
    }
    if ((millis() - touchStartMs) < 150) {
        // Keep LPF running during settle so it's warm by the time we unfreeze
        filteredAccelX = (1.0f - AIR_FUSION_ACCEL_LPF) * filteredAccelX
                       + AIR_FUSION_ACCEL_LPF * (float)gearVR.accelX;
        filteredAccelY = (1.0f - AIR_FUSION_ACCEL_LPF) * filteredAccelY
                       + AIR_FUSION_ACCEL_LPF * (float)gearVR.accelY;
        emaDeltaX = emaDeltaY = 0.0f;
        return;
    }
    
    // Freeze while user is actively scrolling (set by handleScroll)
    if (millis() < scrollLockUntilMs) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
        return;
    }
    
    // Raw gyro minus tracked static bias → angular rate in sensor units.
    // Bias is maintained elsewhere by: (a) 100-sample warmup at connect,
    // (b) accel-gated stillness tracker (slow EMA while still).
    float rX = (float)gearVR.gyroZ - gyroBiasZ;   // mouse X  ← yaw   (rotation around gravity)
    float rY = (float)gearVR.gyroX - gyroBiasX;   // mouse Y  ← pitch (Gear VR puts pitch on X-axis)
    
#if AIR_FUSION_ENABLE
    // === COMPLEMENTARY FILTER (sensor fusion) ===
    // Mix gyro angular rate with accelerometer tilt signal. The accel term
    // acts as a soft anchor: when controller drifts from its "zero" orientation,
    // a small restoring rate is injected, pulling the cursor back to rest —
    // without the "sticky" feel of pure-accel pointing.
    //
    // Axis mapping for Gear VR held horizontally, buttons up:
    //   accelX ≈ lateral tilt  (roll around pitch axis)  → nudges yaw  (mouse X)
    //   accelY ≈ forward tilt  (pitch around roll axis)  → nudges pitch (mouse Y)
    // Subtract accel bias captured at warmup → at rest orientation this is 0,
    // and only ACTUAL tilts (deviations from the zero orientation) contribute.
    // Without this subtraction the ~447 DC offset on Y would pull the cursor
    // down every frame regardless of user intent.
    // LPF on accel BEFORE fusion — kills per-sample noise that produces the
    // "diamond grid" artefact on slow movement. Only the low-frequency tilt
    // trend survives, which is all the fusion correction term should carry.
    if (!accelFilterPrimed) {
        filteredAccelX = (float)gearVR.accelX;
        filteredAccelY = (float)gearVR.accelY;
        accelFilterPrimed = true;
    } else {
        filteredAccelX = (1.0f - AIR_FUSION_ACCEL_LPF) * filteredAccelX
                       + AIR_FUSION_ACCEL_LPF * (float)gearVR.accelX;
        filteredAccelY = (1.0f - AIR_FUSION_ACCEL_LPF) * filteredAccelY
                       + AIR_FUSION_ACCEL_LPF * (float)gearVR.accelY;
    }
    float aTiltX = (filteredAccelX - accelBiasX) * AIR_FUSION_ACCEL_SCALE * AIR_FUSION_ACCEL_X_SIGN;
    float aTiltY = (filteredAccelY - accelBiasY) * AIR_FUSION_ACCEL_SCALE * AIR_FUSION_ACCEL_Y_SIGN;
    rX = rX * AIR_FUSION_W_GYRO + aTiltX * AIR_FUSION_W_ACCEL;
    rY = rY * AIR_FUSION_W_GYRO + aTiltY * AIR_FUSION_W_ACCEL;
#endif
    
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
    
    // === HARD SUBTRACTIVE VECTOR DEADZONE (anti-tremor) ===
    // Quadratic roll-off was letting ~60% of hand-tremor-level signal through
    // (at mag=DZ*0.8 the scale is 0.64), so the cursor visibly jittered while
    // the user was trying to hold still on a button. Replace with a HARD
    // zero-below + linear-ramp-above-threshold rule:
    //   mag < DZ  → output = 0                         (tremor killed cleanly)
    //   mag ≥ DZ  → output = in * (mag - DZ) / mag     (subtractive scaling)
    // Subtractive (not multiplicative) means there is NO step at the boundary:
    // output grows smoothly from 0 as magnitude exceeds DZ. Diagonals stay
    // straight because the same scalar applies to both components.
    float mag = sqrtf(emaDeltaX * emaDeltaX + emaDeltaY * emaDeltaY);
    if (mag < (float)AIR_GYRO_DEADZONE) {
        emaDeltaX = 0.0f;
        emaDeltaY = 0.0f;
    } else if (AIR_GYRO_DEADZONE > 0) {
        float scale = (mag - (float)AIR_GYRO_DEADZONE) / mag;
        emaDeltaX *= scale;
        emaDeltaY *= scale;
    }
    
    // === UNIFIED VECTOR GAIN + SUB-PIXEL FLOAT ACCUMULATOR ===
    // Use ONE scalar gain derived from vector magnitude (not per-axis) so both
    // axes always scale by the same factor → diagonals stay on a straight line.
    // Per-axis independent gains caused subtle direction skew at low rates,
    // which manifested as "diamond grid" staircasing.
    //
    // === SUB-PIXEL VISCOUS EMISSION (truncation-toward-zero) ===
    // Use truncf() instead of roundf() so a pixel is emitted ONLY when the
    // accumulator reaches a full ±1.0. Example: at 0.4 px/frame the accumulator
    // hits 1.2 on frame 3 → emit 1, keep 0.2. The result is a slow, steady drip
    // (1 px every 2-3 frames) instead of roundf()'s twitchy every-other-frame
    // step at half-pixel thresholds. The eye reads this as smooth "oily" motion.
    // Large motions still emit instantly (truncf(5.7)=5, keep 0.7).
    float vmag = sqrtf(emaDeltaX * emaDeltaX + emaDeltaY * emaDeltaY);
    float gain = AIR_GYRO_SENS * (1.0f + AIR_GYRO_ACCEL * vmag / AIR_GYRO_ACCEL_REF);
    remainderX += emaDeltaX * gain;
    remainderY += emaDeltaY * gain;
    
    // Clamp accumulator so a single spike can't queue hundreds of pixels
    // for the NEXT frames to "replay" as a ghost zoom. Anything above this
    // cap is almost certainly spike, not intent.
    const float REMAINDER_CLAMP = 200.0f;
    if (remainderX >  REMAINDER_CLAMP) remainderX =  REMAINDER_CLAMP;
    if (remainderX < -REMAINDER_CLAMP) remainderX = -REMAINDER_CLAMP;
    if (remainderY >  REMAINDER_CLAMP) remainderY =  REMAINDER_CLAMP;
    if (remainderY < -REMAINDER_CLAMP) remainderY = -REMAINDER_CLAMP;
    
    int32_t moveX = (int32_t)truncf(remainderX);   // toward-zero: 1.8→1, -1.8→-1
    int32_t moveY = (int32_t)truncf(remainderY);
    remainderX   -= (float)moveX;                   // fractional part survives
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
//   • Touchpad click L-zone → Mouse LEFT       (debounced)
//   • Touchpad click R-zone → Mouse RIGHT      (debounced)
//   • Trigger               → air-mouse gate   (handled in notifyCallback)
//   • Home                  → Consumer MUTE        (edge-triggered)
//   • Back                  → Consumer PLAY_PAUSE  (edge-triggered)
//   • Volume +/-            → Consumer Volume Inc/Dec (edge-triggered)
void gearvr_update_mouse()
{
    // Double-check BLE state; release any held buttons if the link vanished
    if (!gearVR.connected || !pClient || !pClient->isConnected()) {
        if (mouseLastLeft)  { Mouse.release(MOUSE_LEFT);  mouseLastLeft  = false; }
        if (mouseLastRight) { Mouse.release(MOUSE_RIGHT); mouseLastRight = false; }
        return;
    }
    
    uint32_t now = millis();
    
    bool trigger = gearVR.triggerPressed;
    bool tpClick = gearVR.touchpadClicked;
    bool homeBtn = gearVR.homePressed;
    bool backBtn = gearVR.backPressed;
    bool volUp   = gearVR.volumeUpPressed;
    bool volDn   = gearVR.volumeDownPressed;
    
    // --- Button mapping ---
    //   Touchpad click   → LEFT if finger on left/center half of pad,
    //                      RIGHT if finger on right half   (zone latched at press edge)
    //   Trigger          → air-mouse activation (handled in notifyCallback)
    //   Home             → Consumer MUTE            (media key)
    //   Back             → Consumer PLAY_PAUSE      (media key)
    //   Volume           → Consumer Volume Inc/Dec  (media key)
    //
    // Touchpad X is reported as 10-bit but HW only produces 0..~315
    // empirically (confirmed from live log: max seen = 313). Midpoint = 160.
    // Latch the zone on the rising edge of tpClick so a drag after press
    // can't flip the button mid-hold.
    #define TP_CLICK_RIGHT_ZONE_X  160
    static bool tpClickZoneRight = false;
    if (tpClick && !lastTouchpadClicked) {
        tpClickZoneRight = ((uint16_t)gearVR.touchX > TP_CLICK_RIGHT_ZONE_X);
    }
    bool wantLeft  = tpClick && !tpClickZoneRight;
    bool wantRight = tpClick && tpClickZoneRight;
    (void)trigger;  // reused for air-mouse gating in notifyCallback
    
    // Edge-triggered debug prints for all buttons (instant visual feedback)
    static bool dbgLastTrig = false, dbgLastHome = false, dbgLastBack = false;
    static bool dbgLastVolUp = false, dbgLastVolDn = false;
    if (tpClick  && !lastTouchpadClicked) {
        const char *zs = tpClickZoneRight ? "[BTN] Touchpad R-zone → RIGHT" : "[BTN] Touchpad L-zone → LEFT";
        if (Serial) Serial.printf("%s (x=%u)\n", zs, (unsigned)gearVR.touchX);
        Serial0.printf("%s (x=%u)\n", zs, (unsigned)gearVR.touchX);
    }
    if (trigger  && !dbgLastTrig)         { if (Serial) Serial.println("[BTN] Trigger → AIR MOUSE ON"); Serial0.println("[BTN] Trigger → AIR MOUSE ON"); }
    if (homeBtn  && !dbgLastHome)         { if (Serial) Serial.println("[BTN] Home     → MUTE"); Serial0.println("[BTN] Home     → MUTE"); }
    if (backBtn  && !dbgLastBack)         { if (Serial) Serial.println("[BTN] Back     → PLAY/PAUSE"); Serial0.println("[BTN] Back     → PLAY/PAUSE"); }
    if (volUp    && !dbgLastVolUp)        { if (Serial) Serial.println("[BTN] Vol+"); Serial0.println("[BTN] Vol+"); }
    if (volDn    && !dbgLastVolDn)        { if (Serial) Serial.println("[BTN] Vol-"); Serial0.println("[BTN] Vol-"); }
    dbgLastTrig = trigger; dbgLastHome = homeBtn; dbgLastBack = backBtn;
    dbgLastVolUp = volUp; dbgLastVolDn = volDn;
    
    // --- LEFT click (debounced) ---
    if (wantLeft != mouseLastLeft) {
        if (now - lastLeftChange >= BUTTON_DEBOUNCE_MS) {
            if (wantLeft) { Mouse.press(MOUSE_LEFT);   Serial0.println("[HID] L+"); }
            else          { Mouse.release(MOUSE_LEFT); Serial0.println("[HID] L-"); }
            mouseLastLeft = wantLeft;
            lastLeftChange = now;
        }
    }
    
    // --- RIGHT click (debounced) ---
    if (wantRight != mouseLastRight) {
        if (now - lastRightChange >= BUTTON_DEBOUNCE_MS) {
            if (wantRight) { Mouse.press(MOUSE_RIGHT);   Serial0.println("[HID] R+"); }
            else           { Mouse.release(MOUSE_RIGHT); Serial0.println("[HID] R-"); }
            mouseLastRight = wantRight;
            lastRightChange = now;
        }
    }
    
    // --- Consumer Control: edge-triggered one-shot press/release ---
    // Back button → MEDIA PLAY/PAUSE
    if (backBtn != lastBack) {
        if (backBtn) ConsumerControl.press(HID_CC_PLAY_PAUSE);
        else         ConsumerControl.release();
        lastBack = backBtn;
    }
    
    // Home button → MEDIA MUTE
    if (homeBtn != lastHome) {
        if (homeBtn) ConsumerControl.press(HID_CC_MUTE);
        else         ConsumerControl.release();
        lastHome = homeBtn;
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

// Diagnostic: bias-corrected gyro rates that feed the mouse.
// Computed fresh (not cached) so the reading is valid even when the finger is
// NOT on the pad — useful for "is the mapping correct?" tests where the user
// performs pivot-motions on a table without pressing the touchpad.
void gearvr_get_mouse_rates(float *rX, float *rY)
{
    if (!gyroBiasPrimed) { *rX = 0.0f; *rY = 0.0f; return; }
    *rX = (float)gearVR.gyroZ - gyroBiasZ;   // mouse X ← yaw
    *rY = (float)gearVR.gyroX - gyroBiasX;   // mouse Y ← pitch (X-axis on Gear VR)
}
