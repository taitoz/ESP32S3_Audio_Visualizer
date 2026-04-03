#include "light_sensor.h"
#include "audio_sampling.h"
#include "settings.h"
#include "esp_adc/adc_oneshot.h"

/*******************************************************************************
 * Ambient Light Sensor — smoothed ADC reading → auto backlight PWM
 * 
 * Uses the same ADC1 unit and handle as audio sampling. No new unit is created.
 * The light sensor channel is configured on the existing adc_handle.
 ******************************************************************************/

static float    smoothedVal = 0.0f;
static bool     initialized = false;
static unsigned long lastPollMs = 0;
static uint8_t  computedBrightness = 128;  // default 50% if no sensor

void light_sensor_init()
{
    // Configure light sensor channel on the existing ADC1 handle
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,    // 0–3.3V range (same as audio channels)
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, LIGHT_SENSOR_CHANNEL, &chan_cfg);

    // Seed the smoother with an initial reading
    int seed = 0;
    adc_oneshot_read(adc_handle, LIGHT_SENSOR_CHANNEL, &seed);
    smoothedVal = (float)seed;
    // If sensor reads near zero (not connected), keep default 50%
    computedBrightness = (seed < 5) ? 128 : settings.brightness;
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
    int raw = 0;
    adc_oneshot_read(adc_handle, LIGHT_SENSOR_CHANNEL, &raw);

    // Exponential moving average
    smoothedVal = smoothedVal + LIGHT_SENSOR_SMOOTH * ((float)raw - smoothedVal);

    // Apply gain and map smoothed ADC (0–4095) → brightness (min–max)
    uint8_t bMin = settings.brightness_min;
    uint8_t bMax = settings.brightness_max;
    if (bMin > bMax) bMin = bMax;  // safety

    float gain = settings.light_gain;
    if (gain < 0.1f) gain = 0.1f;
    float norm = (smoothedVal * gain) / 4095.0f;  // apply gain before normalizing
    if (norm > 1.0f) norm = 1.0f;
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
