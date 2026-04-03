#pragma once

#include <Arduino.h>
#include "audio_sampling.h"

/*******************************************************************************
 * Minimal Spectrum Module - FFT Only (for Technics EQ)
 * 
 * Provides FFT computation and frequency band values.
 * All old spectrum visualization code has been removed.
 ******************************************************************************/

#define NUM_BANDS  32   // Frequency bands for FFT output

extern float bandValuesL[NUM_BANDS];
extern float bandValuesR[NUM_BANDS];

void spectrum_init(void);
void spectrum_compute_fft(void);
