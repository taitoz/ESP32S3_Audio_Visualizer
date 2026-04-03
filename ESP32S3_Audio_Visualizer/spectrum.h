#pragma once

#include <Arduino.h>

/*******************************************************************************
 * Minimal Spectrum Module - FFT Only (for Technics EQ)
 * 
 * This provides only the FFT functionality needed for Technics EQ mode.
 * All old spectrum visualization code has been removed.
 ******************************************************************************/

// ─── FFT Constants ───────────────────────────────────────────────────────────
#define SAMPLES            1024
#define SAMPLING_FREQ      22050
#define NUM_BANDS           32      // Frequency bands for FFT output

// ─── External Variables (from audio_sampling) ───────────────────────────────
extern float vRealL[SAMPLES];
extern float vImagL[SAMPLES];
extern float vRealR[SAMPLES];
extern float vImagR[SAMPLES];

// ─── Band Values (FFT output) ───────────────────────────────────────────────
extern float bandValuesL[NUM_BANDS];
extern float bandValuesR[NUM_BANDS];

// ─── Public Interface ───────────────────────────────────────────────────────
void spectrum_init(void);
void spectrum_compute_fft(void);
