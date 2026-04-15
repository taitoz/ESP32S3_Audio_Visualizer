/*******************************************************************************
 * ESP32-S3 Audio Visualizer for LilyGo T-Display-S3-Long (Touchscreen)
 * 
 * Architecture (Dual-Core):
 *   Core 0 (System): BLE (NimBLE), Touch, Serial Commands, RTC, USB HID
 *   Core 1 (App):    Audio Sampling (ISR), FFT, Sprite Rendering, QSPI Push
 * 
 * I2C Buses:
 *   I2C0 (Wire):  Touch controller — GPIO15 (SDA), GPIO10 (SCL)
 *   I2C1 (Wire1): RTC DS3231      — GPIO6 (SDA),  GPIO7 (SCL)
 * 
 * Hardware:
 *   Board:  ESP32-S3-Dev (LilyGo T-Display-S3-Long)
 *   USB CDC On Boot: Enabled
 *   Flash Size: 16MB
 *   Partition Scheme: 16M Flash (3MB APP / 9.9MB FATFS)
 *   PSRAM: OPI PSRAM
 * 
 * Audio Input (Stereo):
 *   LEFT:  Audio transformer → 100nF → GPIO3 (ADC1_CH2) + bias 2x100k
 *   RIGHT: Audio transformer → 100nF → GPIO4 (ADC1_CH3) + bias 2x100k
 * 
 * Based on nikthefix's TFT_eSPI driver for AXS15231B QSPI display.
 ******************************************************************************/

#include "AXS15231B.h"
#include <TFT_eSPI.h>
#include <Wire.h>
#include "pins_config.h"
#include "audio_sampling.h"
#include "technics_vfd.h"
#include "spectrum.h"
#include "settings.h"
#include "serial_cmd.h"
#include "rtc_time.h"
#include "gearvr_controller.h"
#include "esp_task_wdt.h"
#include "USB.h"
#include "USBHIDMouse.h"

// ─── Display & Sprite ───────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// ─── USB HID Mouse ──────────────────────────────────────────────────────────
USBHIDMouse Mouse;

// ─── Touch ──────────────────────────────────────────────────────────────────
uint8_t ALS_ADDRESS = 0x3B;
uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
bool    touch_held = false;
#define TOUCH_DEBOUNCE_MS   300
#define TOUCH_I2C_TIMEOUT   50   // ms timeout for I2C touch read
unsigned long touch_released_at = 0;

// ─── Visualization Mode (volatile — written by Core 0, read by Core 1) ─────
enum VisMode {
    VIS_EQ = 0,            // Technics EQ (SH-GE70)
    VIS_VU,                // Technics VFD VU (RS-TR373)
    VIS_MODE_COUNT
};

volatile VisMode currentMode = VIS_EQ;
volatile bool pendingModeSwitch = false;

// ─── FPS counter ────────────────────────────────────────────────────────────
unsigned long frameCount = 0;
unsigned long lastFpsTime = 0;
volatile float fps = 0;

// ─── FreeRTOS task handles ──────────────────────────────────────────────────
TaskHandle_t audioDisplayTaskHandle = NULL;
TaskHandle_t touchTaskHandle = NULL;
TaskHandle_t gearVRTaskHandle = NULL;

// ─── BLE connect request (async — triggered from serial, runs in gearVRTask)
volatile bool bleConnectRequested = false;
volatile bool bleDisconnectRequested = false;

// ─── Touch Detection (with I2C timeout) ────────────────────────────────────
bool checkTouch()
{
    uint8_t buff[20] = {0};
    Wire.beginTransmission(ALS_ADDRESS);
    Wire.write(read_touchpad_cmd, 8);
    if (Wire.endTransmission() != 0) return false;
    
    Wire.requestFrom(ALS_ADDRESS, (uint8_t)8);
    
    // Wait with timeout instead of infinite loop
    unsigned long start = millis();
    while (!Wire.available()) {
        if (millis() - start > TOUCH_I2C_TIMEOUT) return false;
        vTaskDelay(1);
    }
    Wire.readBytes(buff, 8);

    uint8_t pointNum = AXS_GET_POINT_NUM(buff);
    if (pointNum == 0) return false;

    int pointX = AXS_GET_POINT_X(buff, 0);
    int pointY = AXS_GET_POINT_Y(buff, 0);

    if (pointX > 640) pointX = 640;
    if (pointY > 180) pointY = 180;

    int tx = map(pointX, 627, 10, 0, 640);
    int ty = map(pointY, 180, 0, 0, 180);

    if (tx >= 0 && tx <= SCREEN_WIDTH && ty >= 0 && ty <= SCREEN_HEIGHT) {
        return true;
    }
    return false;
}

void cycleMode()
{
    pendingModeSwitch = true;
}

// ─── Track last mode for background redraw on switch ────────────────────────
static VisMode lastDrawnMode = VIS_MODE_COUNT;

// ─── Core 1 Task: Audio + FFT + Display (heavy math + QSPI push) ───────────
void audioDisplayTask(void *param)
{
    esp_task_wdt_add(NULL);
    lastFpsTime = millis();

    for (;;) {
        esp_task_wdt_reset();
        unsigned long loopStart = millis();
        
        // Handle mode switch from touch
        if (pendingModeSwitch) {
            pendingModeSwitch = false;
            currentMode = (VisMode)((currentMode + 1) % VIS_MODE_COUNT);
            settings.viz_mode = (uint8_t)currentMode;
            Serial.printf("Mode switch -> %d\n", (int)currentMode);
        }

        // Sync viz mode from serial UI
        VisMode serialMode = (VisMode)settings.viz_mode;
        if (serialMode < VIS_MODE_COUNT && serialMode != currentMode) {
            currentMode = serialMode;
        }

        // Redraw background when mode changes
        if (currentMode != lastDrawnMode) {
            sprite.fillSprite(TFT_BLACK);
            lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
            
            technics_vfd_reset_state();
            if (currentMode == VIS_EQ) technics_vfd_draw_bg_eq(tft);
            else                        technics_vfd_draw_bg_vu(tft);
            lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
            lastDrawnMode = currentMode;
        }

        if (audio_sampling_is_ready()) {
            audio_sampling_consume();

            float rmsL  = audio_get_rms(CH_LEFT);
            float rmsR  = audio_get_rms(CH_RIGHT);

            unsigned long frameStart = millis();

            if (currentMode == VIS_EQ) {
                spectrum_compute_fft();
                technics_vfd_draw_eq(tft, bandValuesL, NUM_BANDS);
            } else if (currentMode == VIS_VU) {
                technics_vfd_draw_vu(tft, rmsL, rmsR);
            }

            // Push full frame to QSPI display
            lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
            vTaskDelay(pdMS_TO_TICKS(2));  // Yield briefly for system tasks

            unsigned long frameTime = millis() - frameStart;

            // FPS calculation
            frameCount++;
            unsigned long now = millis();
            if (now - lastFpsTime >= 1000) {
                fps = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
                frameCount = 0;
                lastFpsTime = now;
            }
        }
        
        // Frame rate limiting
        unsigned long elapsed = millis() - loopStart;
        if (elapsed < 33) {
            vTaskDelay(pdMS_TO_TICKS(33 - elapsed));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}

// ─── Core 0 Task: Touch + Auto-brightness ──────────────────────────────────
void touchTask(void *param)
{
    Serial.println("[Core0] Touch task started");
    
    for (;;) {
        // Auto-brightness (once per second)
        static uint32_t lastLightCheck = 0;
        if (settings.auto_brightness && (millis() - lastLightCheck > 1000)) {
            int raw = audio_read_light_sensor();
            float scaled = raw * settings.light_gain;
            if (scaled > 4095.0f) scaled = 4095.0f;
            int targetBri = map((int)scaled, 0, 4095, settings.brightness_min, settings.brightness_max);
            targetBri = constrain(targetBri, settings.brightness_min, settings.brightness_max);
            analogWrite(TFT_BL, targetBri);
            lastLightCheck = millis();
        }

        // Touch handling with I2C timeout
        int touchInt = digitalRead(TOUCH_INT);
        if (touchInt == LOW) {
            if (!touch_held && (millis() - touch_released_at >= TOUCH_DEBOUNCE_MS)) {
                if (checkTouch()) {
                    cycleMode();
                }
            }
            touch_held = true;
        } else {
            if (touch_held) {
                touch_released_at = millis();
            }
            touch_held = false;
        }

        vTaskDelay(pdMS_TO_TICKS(20));  // 50 Hz touch polling
    }
}

// ─── Core 0 Task: BLE + RTC (fully automatic) ──────────────────────────────
void bleRtcTask(void *param)
{
    Serial.println("[Core0] BLE+RTC task started");
    
    // Initial auto-connect attempt after 2 seconds
    vTaskDelay(pdMS_TO_TICKS(2000));
    Serial.println("[BLE] Starting auto-connect...");
    gearvr_connect();
    
    for (;;) {
        // Handle manual connect/disconnect requests from serial (optional)
        if (bleConnectRequested) {
            bleConnectRequested = false;
            Serial.println("[BLE] Manual connect requested...");
            gearvr_connect();
        }
        
        if (bleDisconnectRequested) {
            bleDisconnectRequested = false;
            Serial.println("[BLE] Manual disconnect requested...");
            gearvr_disconnect();
        }
        
        // ALWAYS call gearvr_update() - it handles auto-reconnect and keep-alive
        gearvr_update();
        
        // RTC time update (every second, uses I2C1 — no conflict with touch)
        // Non-blocking: if RTC not found, this does nothing
        static uint32_t lastRtcUpdate = 0;
        if (millis() - lastRtcUpdate > 1000) {
            rtc_update_time();
            lastRtcUpdate = millis();
        }
        
        // Stack/heap monitoring (every 60 seconds)
        static uint32_t lastMonitor = 0;
        if (millis() - lastMonitor > 60000) {
            Serial.printf("[MON] Heap: %u free, %u min | BLE+RTC stack: %u\n",
                ESP.getFreeHeap(),
                ESP.getMinFreeHeap(),
                uxTaskGetStackHighWaterMark(NULL));
            lastMonitor = millis();
        }
        
        vTaskDelay(pdMS_TO_TICKS(50));  // 20 Hz update rate
    }
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("ESP32-S3 Audio Visualizer starting...");
    
    // Reconfigure watchdog: exclude IDLE tasks (NimBLE uses CPU 0 heavily)
    esp_task_wdt_deinit();
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = 0,   // Don't monitor IDLE tasks
        .trigger_panic = true
    };
    esp_task_wdt_init(&wdt_config);
    Serial.println("WDT reconfigured: IDLE excluded, 30s timeout");

    // ── Touch screen init (I2C0) ──
    Serial.println("Initializing touch screen...");
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RES, OUTPUT);
    
    Serial.printf("Touch: INT=%d, RES=%d, SDA=%d, SCL=%d\n", 
                  TOUCH_INT, TOUCH_RES, TOUCH_IICSDA, TOUCH_IICSCL);
    
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    digitalWrite(TOUCH_RES, LOW);  delay(10);
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    
    Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);
    Wire.setClock(400000);
    
    Wire.beginTransmission(ALS_ADDRESS);
    int result = Wire.endTransmission();
    Serial.printf("Touch I2C: %s\n", result == 0 ? "OK" : "FAIL");

    // ── Settings (NVS) ──
    settings_init();

    // ── Display init (CRITICAL ORDER: init → clear → backlight) ──
    Serial.println("Initializing display...");
    
    // 1. Initialize QSPI bus and AXS15231B controller
    axs15231_init();
    
    // 2. Create sprite buffer and clear to black
    sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);
    sprite.setSwapBytes(1);
    sprite.fillSprite(TFT_BLACK);
    
    // 3. Push black frame to display
    lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
    delay(50);  // Wait for frame to be displayed
    
    // 4. NOW enable backlight (screen is already black)
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);  // Start with backlight OFF
    delay(10);
    analogWrite(TFT_BL, settings.brightness);  // Fade in
    
    Serial.println("Display ready");

    // ── Audio sampling init ──
    audio_sampling_init();
    spectrum_init();

    // ── Serial command handler ──
    serial_cmd_init();

    // ── RTC DS3231 init (I2C1 — separate bus) ──
    Serial.println("Initializing RTC...");
    rtc_init();
    Serial.println("RTC init done");

    // ── USB HID Mouse init (USB-OTG) ──
    Serial.println("Initializing USB HID...");
    
    // Configure USB device descriptor
    USB.VID(0xCAFE);  // Custom Vendor ID
    USB.PID(0x0001);  // Custom Product ID
    USB.productName("ESP32-S3 Audio Visualizer");
    USB.manufacturerName("Taito");
    
    // Start USB stack (non-blocking)
    USB.begin();
    
    // Initialize HID Mouse (requires USB.begin() first)
    Mouse.begin();
    
    // Note: Serial output goes through USB CDC automatically on ESP32-S3
    // USB not connected? No problem - code continues without blocking
    Serial.println("USB HID Mouse ready (non-blocking mode)");
    
    // ── Gear VR BLE init (NimBLE) ──
    Serial.println("Initializing NimBLE...");
    gearvr_init();
    Serial.println("NimBLE init done");

    // ── Splash screen ──
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_CYAN, TFT_BLACK);
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("ESP32-S3 AUDIO VISUALIZER", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 2);
    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.drawString("Technics VFD Legacy", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 10, 1);
    lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
    delay(1500);

    // ── Technics VFD module ──
    technics_vfd_init(tft);
    currentMode = VIS_EQ;
    lastDrawnMode = VIS_MODE_COUNT;

    // ── Launch FreeRTOS tasks ──
    // Core 1: Heavy rendering (FFT + sprite + QSPI push)
    BaseType_t r1 = xTaskCreatePinnedToCore(audioDisplayTask, "AudioDisp", 32768, NULL, 1, &audioDisplayTaskHandle, 1);
    
    // Core 0: Touch polling (lightweight, needs I2C0)
    BaseType_t r2 = xTaskCreatePinnedToCore(touchTask, "Touch", 4096, NULL, 2, &touchTaskHandle, 0);
    
    // Core 0: BLE + RTC (handles blocking BLE operations separately from touch)
    BaseType_t r3 = xTaskCreatePinnedToCore(bleRtcTask, "BLE_RTC", 8192, NULL, 1, &gearVRTaskHandle, 0);
    
    Serial.printf("Tasks: AudioDisp=%s, Touch=%s, BLE_RTC=%s\n",
        r1 == pdPASS ? "OK" : "FAIL",
        r2 == pdPASS ? "OK" : "FAIL",
        r3 == pdPASS ? "OK" : "FAIL");

    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    
    // ── DAC AK4493 status (not implemented yet) ──
    Serial.println("DAC AK4493: Not connected (stub)");
    
    Serial.println("Ready. Touch to cycle: EQ -> VU");
}

// ─── Loop: Serial command processing (runs on Core 1 loopTask) ─────────────
void loop()
{
    static uint32_t lastLoopLog = 0;
    if (millis() - lastLoopLog > 30000) {
        Serial.printf("[Loop] Running on Core %d, polling serial...\n", xPortGetCoreID());
        lastLoopLog = millis();
    }
    
    serial_cmd_poll();
    
    // Update USB HID Mouse from Gear VR touchpad
    gearvr_update_mouse();
    
    vTaskDelay(pdMS_TO_TICKS(10));  // 100 Hz serial polling
}
