/*******************************************************************************
 * ESP32-S3 Audio Visualizer for LilyGo T-Display-S3-Long (Touchscreen)
 * 
 * Features:
 *   - Spectrum Analyzer (FFT from GPIO ADC via audio transformer)
 *   - VU Meters with multiple styles (Needle, LED Ladder, Retro Analog)
 *   - Touch to cycle visualization modes
 *   - AK4493 DAC control via SPI (Phase 3 — stub included)
 *   - BLE Gear VR controller + USB HID mouse (Phase 4/5 — future)
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
#include "spectrum.h"
#include "vu_meter.h"

// ─── Display & Sprite ───────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);

// ─── Touch ──────────────────────────────────────────────────────────────────
uint8_t ALS_ADDRESS = 0x3B;
uint8_t read_touchpad_cmd[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x0, 0x0, 0x0, 0x8};
bool    touch_held = false;
#define TOUCH_TIMEOUT_RESET 30000
uint16_t touch_timeout = 0;

// ─── Visualization Mode ─────────────────────────────────────────────────────
enum VisMode {
    VIS_SPECTRUM = 0,
    VIS_VU_NEEDLE,
    VIS_VU_LED_LADDER,
    VIS_VU_RETRO,
    VIS_MODE_COUNT
};

VisMode currentMode = VIS_SPECTRUM;

// ─── FPS counter ────────────────────────────────────────────────────────────
unsigned long frameCount = 0;
unsigned long lastFpsTime = 0;
float fps = 0;

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
}

// ─── Draw Frame ─────────────────────────────────────────────────────────────
void drawFrame()
{
    sprite.fillSprite(TFT_BLACK);

    switch (currentMode) {
        case VIS_SPECTRUM:
            spectrum_draw_bars(sprite);
            break;
        case VIS_VU_NEEDLE:
            vu_meter_draw(sprite, VU_STYLE_NEEDLE);
            break;
        case VIS_VU_LED_LADDER:
            vu_meter_draw(sprite, VU_STYLE_LED_LADDER);
            break;
        case VIS_VU_RETRO:
            vu_meter_draw(sprite, VU_STYLE_RETRO);
            break;
        default:
            spectrum_draw_bars(sprite);
            break;
    }

    // Mode indicator at bottom-right
    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.setTextDatum(BR_DATUM);
    const char* modeNames[] = {"SPECTRUM", "VU NEEDLE", "VU LED", "VU RETRO"};
    sprite.drawString(modeNames[currentMode], SCREEN_WIDTH - 5, SCREEN_HEIGHT - 2, 1);

    // FPS at bottom-left
    sprite.setTextDatum(BL_DATUM);
    char fpsBuf[16];
    snprintf(fpsBuf, sizeof(fpsBuf), "%.0f FPS", fps);
    sprite.drawString(fpsBuf, 5, SCREEN_HEIGHT - 2, 1);

    // Push to display (software-rotated 90°)
    lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
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

    // Display init
    sprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT);  // full screen landscape sprite in PSRAM
    sprite.setSwapBytes(1);
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    axs15231_init();

    // Audio sampling init
    audio_sampling_init();
    spectrum_init();
    vu_meter_init();

    // Show splash
    sprite.fillSprite(TFT_BLACK);
    sprite.setTextColor(TFT_CYAN, TFT_BLACK);
    sprite.setTextDatum(MC_DATUM);
    sprite.drawString("ESP32-S3 AUDIO VISUALIZER", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 - 20, 2);
    sprite.setTextColor(TFT_DARKGREY, TFT_BLACK);
    sprite.drawString("Touch screen to change mode", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 10, 1);
    sprite.drawString("Stereo ADC: L=GPIO3  R=GPIO4", SCREEN_WIDTH/2, SCREEN_HEIGHT/2 + 25, 1);
    lcd_PushColors_rotated_90(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, (uint16_t*)sprite.getPointer());
    delay(2000);

    lastFpsTime = millis();
    Serial.println("Ready. Touch to cycle: Spectrum → VU Needle → VU LED → VU Retro");
}

// ─── Loop ───────────────────────────────────────────────────────────────────
void loop()
{
    // ── Touch handling ──
    if (digitalRead(TOUCH_INT) == LOW) {
        if (!touch_held) {
            if (checkTouch()) {
                cycleMode();
                Serial.printf("Mode: %d\n", currentMode);
            }
        }
        touch_held = true;
        touch_timeout = 0;
    }
    touch_timeout++;
    if (touch_timeout >= TOUCH_TIMEOUT_RESET) {
        touch_held = false;
        touch_timeout = TOUCH_TIMEOUT_RESET;
    }

    // ── Audio processing ──
    if (audio_sampling_is_ready()) {
        audio_sampling_consume();

        float rmsL  = audio_get_rms(CH_LEFT);
        float peakL = audio_get_peak(CH_LEFT);
        float rmsR  = audio_get_rms(CH_RIGHT);
        float peakR = audio_get_peak(CH_RIGHT);

        // Update VU meter ballistics (stereo)
        vu_meter_update(rmsL, peakL, rmsR, peakR);

        // Run FFT for spectrum mode (always compute so switching is seamless)
        spectrum_compute_fft();

        // Draw current visualization
        drawFrame();

        // FPS calculation
        frameCount++;
        unsigned long now = millis();
        if (now - lastFpsTime >= 1000) {
            fps = (float)frameCount * 1000.0f / (float)(now - lastFpsTime);
            frameCount = 0;
            lastFpsTime = now;
        }
    }
}
