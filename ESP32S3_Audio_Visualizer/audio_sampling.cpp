#include "audio_sampling.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

/*******************************************************************************
 * Timer-driven ADC sampling into double-buffer
 ******************************************************************************/

volatile bool bufferReady = false;
volatile int  activeBuffer = 0;
int16_t       sampleBuffer[2][SAMPLES];
double        vReal[SAMPLES];
double        vImag[SAMPLES];

static volatile uint16_t sampleIndex = 0;
static esp_timer_handle_t samplingTimer = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

// ISR-safe timer callback — reads one ADC sample per tick
static void IRAM_ATTR sampling_timer_cb(void* arg)
{
    if (bufferReady) return;  // previous buffer not consumed yet, skip

    int raw = 0;
    adc_oneshot_read(adc_handle, AUDIO_ADC_CHANNEL, &raw);

    sampleBuffer[activeBuffer][sampleIndex] = (int16_t)(raw - ADC_CENTER);
    sampleIndex++;

    if (sampleIndex >= SAMPLES) {
        sampleIndex = 0;
        bufferReady = true;
        activeBuffer ^= 1;  // swap buffer
    }
}

void audio_sampling_init()
{
    // Configure ADC oneshot
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,    // 0–3.3V range
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, AUDIO_ADC_CHANNEL, &chan_cfg);

    // Start periodic timer for sampling
    const esp_timer_create_args_t timer_args = {
        .callback = sampling_timer_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "adc_sample",
    };
    esp_timer_create(&timer_args, &samplingTimer);
    esp_timer_start_periodic(samplingTimer, 1000000 / SAMPLING_FREQ);  // period in microseconds
}

void audio_sampling_stop()
{
    if (samplingTimer) {
        esp_timer_stop(samplingTimer);
        esp_timer_delete(samplingTimer);
        samplingTimer = NULL;
    }
    if (adc_handle) {
        adc_oneshot_del_unit(adc_handle);
        adc_handle = NULL;
    }
}

bool audio_sampling_is_ready()
{
    return bufferReady;
}

void audio_sampling_consume()
{
    int readBuf = activeBuffer ^ 1;  // the buffer that just finished filling
    for (int i = 0; i < SAMPLES; i++) {
        vReal[i] = (double)sampleBuffer[readBuf][i];
        vImag[i] = 0.0;
    }
    bufferReady = false;
}

float audio_get_rms()
{
    double sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sum += vReal[i] * vReal[i];
    }
    return sqrt(sum / SAMPLES);
}

float audio_get_peak()
{
    double peak = 0;
    for (int i = 0; i < SAMPLES; i++) {
        double v = fabs(vReal[i]);
        if (v > peak) peak = v;
    }
    return (float)peak;
}
