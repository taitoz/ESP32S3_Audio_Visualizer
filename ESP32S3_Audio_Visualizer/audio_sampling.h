#pragma once

#include <Arduino.h>
#include "pins_config.h"

/*******************************************************************************
 * Audio ADC Sampling — Timer-driven double-buffer for FFT
 * 
 * Hardware setup:
 *   Audio transformer secondary → 100nF cap → ADC pin (GPIO3)
 *   Bias network: 2x 100k from 3.3V and GND to ADC pin (sets DC midpoint ~1.65V)
 ******************************************************************************/

#define SAMPLES            1024       // FFT size (must be power of 2)
#define SAMPLING_FREQ      22050      // Hz — Nyquist = 11025 Hz, enough for audio
#define ADC_RESOLUTION     12         // ESP32-S3 ADC is 12-bit
#define ADC_MAX_VALUE      4095
#define ADC_CENTER         2048       // DC midpoint with bias network

// Double-buffer: one fills while the other is processed
extern volatile bool bufferReady;
extern volatile int  activeBuffer;
extern double        vReal[SAMPLES];
extern double        vImag[SAMPLES];
extern int16_t       sampleBuffer[2][SAMPLES];

void audio_sampling_init();
void audio_sampling_stop();
bool audio_sampling_is_ready();
void audio_sampling_consume();   // copies active buffer into vReal[], clears vImag[], marks consumed
float audio_get_rms();           // compute RMS from current vReal[] (call after consume)
float audio_get_peak();          // compute peak from current vReal[] (call after consume)
