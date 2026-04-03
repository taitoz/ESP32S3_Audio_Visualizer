/*******************************************************************************
 * ESP32-S3 Audio Visualizer for LilyGo T-Display-S3-Long (Touchscreen)
 * 
 * Features:
 *   - Spectrum Analyzer (FFT from GPIO ADC via audio transformer)
 *   - VU Meters with multiple styles (Needle, LED Ladder)
 *   - Touch to cycle visualization modes
 *   - AK4493 DAC control via SPI (Phase 3 — stub included)
 *   - BLE Gear VR controller + USB HID mouse (Phase 4/5 — future)
 * 
 * Architecture:
 *   Core 1 — Audio sampling (timer ISR), FFT, VU update, display rendering
 *   Core 0 — Touch polling + UI interaction
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
#include "light_sensor.h"

// ─── Display & Sprite ───────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// ─── Touch ──────────────────────────────────────────────────────────────────
uint8_t ALS_ADDRESS = 0x3B;
uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
bool    touch_held = false;
#define TOUCH_DEBOUNCE_MS   300   // ignore repeated touches for 300ms after a tap
unsigned long touch_released_at = 0;

// ─── Visualization Mode (volatile — written by Core 0, read by Core 1) ─────
enum VisMode {
    VIS_EQ = 0,            // Technics EQ (SH-GE70)
    VIS_VU,                // Technics VFD VU (RS-TR373)
    VIS_MODE_COUNT
};

volatile VisMode currentMode = VIS_EQ;

// ─── FPS counter ────────────────────────────────────────────────────────────
unsigned long frameCount = 0;
unsigned long lastFpsTime = 0;
volatile float fps = 0;

// ─── FreeRTOS task handles ──────────────────────────────────────────────────
TaskHandle_t audioDisplayTaskHandle = NULL;
TaskHandle_t touchTaskHandle = NULL;

// ─── Touch Detection ────────────────────────────────────────────────────────
bool checkTouch()
{
    uint8_t buff[20] = {0};
    Wire.beginTransmission(ALS_ADDRESS);
    Wire.write(read_touchpad_cmd, 8);
    Wire.endTransmission();
    Wire.requestFrom(ALS_ADDRESS, (uint8_t)8);
    while (!Wire.available());
    Wire.readBytes(buff, 8);

    // Must have at least 1 touch point — otherwise it's not a real touch
    uint8_t pointNum = AXS_GET_POINT_NUM(buff);
    if (pointNum == 0) return false;

    int pointX = AXS_GET_POINT_X(buff, 0);
    int pointY = AXS_GET_POINT_Y(buff, 0);

    if (pointX > 640) pointX = 640;
    if (pointY > 180) pointY = 180;

    int tx = map(pointX, 627, 10, 0, 640);
    int ty = map(pointY, 180, 0, 0, 180);

    // Valid touch anywhere on screen → mode switch
    if (tx >= 0 && tx <= SCREEN_WIDTH && ty >= 0 && ty <= SCREEN_HEIGHT) {
        return true;
    }
    return false;
}

void cycleMode()
{
    currentMode = (VisMode)((currentMode + 1) % VIS_MODE_COUNT);
    settings.viz_mode = (uint8_t)currentMode;
}

// ─── Draw FPS overlay (top center, minimal) ────────────────────────────────
static void drawFPS()
{
    tft.setTextColor(VFD_GRID, VFD_BG);
    tft.setTextDatum(TC_DATUM);
    char fpsBuf[16];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS", fps);
    tft.fillRect(280, 0, 80, 14, VFD_BG);  // Clear previous text
    tft.drawString(fpsBuf, 320, 2, 1);
}

// ─── Track last mode for background redraw on switch ────────────────────────
static VisMode lastDrawnMode = VIS_MODE_COUNT;  // Force first draw

// ─── Core 1 Task: Audio + FFT + Display ─────────────────────────────────────
void audioDisplayTask(void *param)
{
    lastFpsTime = millis();

    for (;;) {
        // Sync viz mode from serial UI
        VisMode serialMode = (VisMode)settings.viz_mode;
        if (serialMode < VIS_MODE_COUNT && serialMode != currentMode) {
            currentMode = serialMode;
        }

        // Redraw background when mode changes
        if (currentMode != lastDrawnMode) {
            if (currentMode == VIS_EQ) technics_vfd_draw_bg_eq(tft);
            else                        technics_vfd_draw_bg_vu(tft);
            lastDrawnMode = currentMode;
        }

        if (audio_sampling_is_ready()) {
            audio_sampling_consume();

            float rmsL  = audio_get_rms(CH_LEFT);
            float rmsR  = audio_get_rms(CH_RIGHT);

            // Run FFT only for EQ mode
            if (currentMode == VIS_EQ) {
                spectrum_compute_fft();
                technics_vfd_draw_eq(tft, bandValuesL, NUM_BANDS);
            } else {
                technics_vfd_draw_vu(tft, rmsL, rmsR);
            }

            // FPS overlay
            drawFPS();

            unsigned long frameTime = millis() - millis();

            // FPS calculation
            frameCount++;
            unsigned long now = millis();
            if (now - lastFpsTime >= 1000) {
                fps = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
                frameCount = 0;
                lastFpsTime = now;
                
                // Print frame timing every 3 seconds
                static unsigned long lastPrint = 0;
                if (now - lastPrint >= 3000) {
                    Serial.printf("FPS: %.1f, Frame time: %lums\n", fps, frameTime);
                    lastPrint = now;
                }
            }
        }
        vTaskDelay(1);  // yield briefly to avoid WDT
    }
}

// ─── Core 0 Task: Touch Polling ─────────────────────────────────────────────
void touchTask(void *param)
{
    for (;;) {
        // Touch handling
        if (digitalRead(TOUCH_INT) == LOW) {
            if (!touch_held && (millis() - touch_released_at >= TOUCH_DEBOUNCE_MS)) {
                if (checkTouch()) {
                    cycleMode();
                    Serial.printf("Mode: %d\n", (int)currentMode);
                }
            }
            touch_held = true;
        } else {
            if (touch_held) {
                touch_released_at = millis();
            }
            touch_held = false;
        }

        // Process serial commands from Web Serial UI
        serial_cmd_poll();

        // Auto-brightness from ambient light sensor
        light_sensor_poll();

        vTaskDelay(pdMS_TO_TICKS(20));  // ~50 Hz touch + serial polling
    }
}

// ─── Setup ──────────────────────────────────────────────────────────────────
void setup()
{
    Serial.begin(115200);
    Serial.println("ESP32-S3 Audio Visualizer starting...");

    // Touch screen init
    pinMode(TOUCH_INT, INPUT_PULLUP);
    pinMode(TOUCH_RES, OUTPUT);
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    digitalWrite(TOUCH_RES, LOW);  delay(10);
    digitalWrite(TOUCH_RES, HIGH); delay(2);
    Wire.begin(TOUCH_IICSDA, TOUCH_IICSCL);
    Wire.setClock(400000);  // I2C Fast Mode — reduces touch polling bus hold time

    // Settings init (load from NVS)
    settings_init();

    // Display init
    sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);  // full screen landscape sprite in PSRAM
    sprite.setSwapBytes(1);
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, settings.brightness);
    axs15231_init();

    // Audio sampling init
    audio_sampling_init();
    spectrum_init();
    
    // Initialize Technics VFD module (sprites only, no heavy images)
    technics_vfd_init(tft);

    // Start in EQ mode
    currentMode = VIS_EQ;
    lastDrawnMode = VIS_MODE_COUNT;  // Force background draw on first frame

    // Serial command handler init
    serial_cmd_init();

    // Light sensor init (auto-brightness)
    light_sensor_init();

    // Show splash
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_CYAN, TFT_BLACK);
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("ESP32-S3 AUDIO VISUALIZER", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 2);
    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.drawString("Touch screen to change mode", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 10, 1);
    sprite.drawString("Settings: open settings.html via USB Serial", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 25, 1);
    lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
    delay(2000);

    // Launch FreeRTOS tasks on separate cores
    xTaskCreatePinnedToCore(audioDisplayTask, "AudioDisplay", 8192, NULL, 2, &audioDisplayTaskHandle, 1);
    xTaskCreatePinnedToCore(touchTask, "Touch", 4096, NULL, 1, &touchTaskHandle, 0);

    Serial.println("Ready. Touch to cycle: Spectrum -> VU Needle -> VU LED");
}

// ─── Loop (unused — work is done in FreeRTOS tasks) ─────────────────────────
void loop()
{
    vTaskDelay(portMAX_DELAY);  // suspend loop task forever
}
