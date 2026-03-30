#include "audio_sampling.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"

/*******************************************************************************
 * Timer-driven ADC sampling into double-buffer (Stereo — L + R)
 * Both channels are sampled on each timer tick (interleaved reads).
 ******************************************************************************/

volatile bool bufferReady = false;
volatile int  activeBuffer = 0;
int16_t       sampleBufferL[2][SAMPLES];
int16_t       sampleBufferR[2][SAMPLES];
double        vRealL[SAMPLES];
double        vImagL[SAMPLES];
double        vRealR[SAMPLES];
double        vImagR[SAMPLES];

static volatile uint16_t sampleIndex = 0;
static esp_timer_handle_t samplingTimer = NULL;
static adc_oneshot_unit_handle_t adc_handle = NULL;

// Timer callback — reads both L and R ADC channels per tick
static void IRAM_ATTR sampling_timer_cb(void* arg)
{
    if (bufferReady) return;  // previous buffer not consumed yet, skip

    int rawL = 0, rawR = 0;
    adc_oneshot_read(adc_handle, AUDIO_ADC_CHANNEL_L, &rawL);
    adc_oneshot_read(adc_handle, AUDIO_ADC_CHANNEL_R, &rawR);

    sampleBufferL[activeBuffer][sampleIndex] = (int16_t)(rawL - ADC_CENTER);
    sampleBufferR[activeBuffer][sampleIndex] = (int16_t)(rawR - ADC_CENTER);
    sampleIndex++;

    if (sampleIndex >= SAMPLES) {
        sampleIndex = 0;
        bufferReady = true;
        activeBuffer ^= 1;  // swap buffer
    }
}

void audio_sampling_init()
{
    // Configure ADC oneshot — single unit, two channels
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    adc_oneshot_new_unit(&init_cfg, &adc_handle);

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_11,    // 0–3.3V range
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_oneshot_config_channel(adc_handle, AUDIO_ADC_CHANNEL_L, &chan_cfg);
    adc_oneshot_config_channel(adc_handle, AUDIO_ADC_CHANNEL_R, &chan_cfg);

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
        vRealL[i] = (double)sampleBufferL[readBuf][i];
        vImagL[i] = 0.0;
        vRealR[i] = (double)sampleBufferR[readBuf][i];
        vImagR[i] = 0.0;
    }
    bufferReady = false;
}

float audio_get_rms(int ch)
{
    double *v = (ch == CH_LEFT) ? vRealL : vRealR;
    double sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sum += v[i] * v[i];
    }
    return sqrt(sum / SAMPLES);
}

float audio_get_peak(int ch)
{
    double *v = (ch == CH_LEFT) ? vRealL : vRealR;
    double peak = 0;
    for (int i = 0; i < SAMPLES; i++) {
        double a = fabs(v[i]);
        if (a > peak) peak = a;
    }
    return (float)peak;
}
