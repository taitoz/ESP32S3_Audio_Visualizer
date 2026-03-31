#include "light_sensor.h"
#include "esp_adc/adc_oneshot.h"

/*******************************************************************************
 * Ambient Light Sensor — smoothed ADC reading → auto backlight PWM
 * 
 * Uses the same ADC1 unit as audio sampling. Since audio sampling already
 * initializes ADC1, this module configures only its own channel on the
 * existing unit. However, because adc_oneshot handles are per-unit and
 * audio_sampling owns the handle, we use a simple analogRead() here instead
 * to avoid handle conflicts. analogRead() is fine at 5 Hz polling rate.
 ******************************************************************************/

static float    smoothedVal = 0.0f;
static bool     initialized = false;
static unsigned long lastPollMs = 0;
static uint8_t  computedBrightness = 255;

void light_sensor_init()
{
    pinMode(LIGHT_SENSOR_PIN, INPUT);
    // Seed the smoother with an initial reading
    smoothedVal = (float)analogRead(LIGHT_SENSOR_PIN);
    computedBrightness = settings.brightness;
    initialized = true;
}

void light_sensor_poll()
{
    if (!initialized) return;
    if (!settings.auto_brightness) return;

    unsigned long now = millis();
    if (now - lastPollMs < LIGHT_SENSOR_POLL_MS) return;
    lastPollMs = now;

    // Read ambient light (0–4095, 12-bit)
    int raw = analogRead(LIGHT_SENSOR_PIN);

    // Exponential moving average
    smoothedVal = smoothedVal + LIGHT_SENSOR_SMOOTH * ((float)raw - smoothedVal);

    // Map smoothed ADC (0–4095) → brightness (min–max)
    uint8_t bMin = settings.brightness_min;
    uint8_t bMax = settings.brightness_max;
    if (bMin > bMax) bMin = bMax;  // safety

    float norm = smoothedVal / 4095.0f;  // 0.0 (dark) → 1.0 (bright)
    uint8_t bri = bMin + (uint8_t)(norm * (float)(bMax - bMin));

    computedBrightness = bri;

    // Apply to backlight
    analogWrite(TFT_BL, bri);
}

int light_sensor_raw()
{
    return (int)smoothedVal;
}

uint8_t light_sensor_brightness()
{
    return computedBrightness;
}
