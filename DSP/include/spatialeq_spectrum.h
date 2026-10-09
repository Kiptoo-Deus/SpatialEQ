// Spectrum analyser for visualisation: drains the engine's analysis ring, runs a windowed FFT and
// returns smoothed log-spaced bands (20 Hz - 20 kHz) normalised to 0..1.
// Call from a single UI/analysis thread (it is the analysis ring's only consumer).
#ifndef SPATIALEQ_SPECTRUM_H
#define SPATIALEQ_SPECTRUM_H

#include "spatialeq_dsp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sq_spectrum sq_spectrum;

sq_spectrum* sq_spectrum_create(int numBands);
void sq_spectrum_destroy(sq_spectrum* s);

// Pulls new audio from the engine and refreshes `outBands` (numBands floats).
// Returns the overall level 0..1. When no new audio arrived the bands decay towards silence.
float sq_spectrum_update(sq_spectrum* s, sq_engine* e, double sampleRate, float* outBands);

// Centre frequency of band i (Hz).
float sq_spectrum_band_frequency(const sq_spectrum* s, int band);

#ifdef __cplusplus
}
#endif

#endif
