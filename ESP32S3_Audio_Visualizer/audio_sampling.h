#pragma once

#include <Arduino.h>
#include "pins_config.h"
#include "esp_adc/adc_oneshot.h"

/*******************************************************************************
 * Audio ADC Sampling — Timer-driven double-buffer for FFT (Stereo)
 * 
 * Hardware setup (each channel identical):
 *   Audio transformer secondary → 100nF cap → ADC pin
 *   Bias network: 2x 100k from 3.3V and GND to ADC pin (DC midpoint ~1.65V)
 * 
 *   LEFT:  GPIO3 (ADC1_CH2)
 *   RIGHT: GPIO4 (ADC1_CH3)
 ******************************************************************************/

#define SAMPLES            1024       // FFT size per channel (must be power of 2)
#define SAMPLING_FREQ      22050      // Hz — Nyquist = 11025 Hz, enough for audio
#define ADC_RESOLUTION     12         // ESP32-S3 ADC is 12-bit
#define ADC_MAX_VALUE      4095
#define ADC_CENTER         2048       // DC midpoint with bias network
#define NOISE_GATE_RMS     30.0f      // RMS below this → silence (handles floating pins & ADC noise)

// Channel indices
#define CH_LEFT  0
#define CH_RIGHT 1

// Double-buffer per channel: one fills while the other is processed
extern volatile bool bufferReady;
extern volatile int  activeBuffer;
extern float         vRealL[SAMPLES];  // Left channel FFT input
extern float         vImagL[SAMPLES];
extern float         vRealR[SAMPLES];  // Right channel FFT input
extern float         vImagR[SAMPLES];
extern int16_t       sampleBufferL[2][SAMPLES];
extern int16_t       sampleBufferR[2][SAMPLES];

// Global ADC handle — can be shared with other modules (e.g., light sensor)
extern adc_oneshot_unit_handle_t adc_handle;

void audio_sampling_init();
void audio_sampling_stop();
bool audio_sampling_is_ready();
void audio_sampling_consume();   // copies both channels into vReal/vImag arrays, marks consumed
float audio_get_rms(int ch);     // compute RMS for channel (CH_LEFT or CH_RIGHT)
float audio_get_peak(int ch);    // compute peak for channel (CH_LEFT or CH_RIGHT)
