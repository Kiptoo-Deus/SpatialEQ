// SpatialEQ DSP core: C interface over the C++ engine.
//
// Threading contract:
//   - sq_engine_prepare / sq_engine_reset: call only while the audio IO is stopped.
//   - sq_engine_set_params / sq_engine_set_head_yaw: any single non-audio thread, lock-free.
//   - sq_engine_process*: the audio thread only. Never allocates, never locks.
//   - sq_engine_read_analysis: one consumer thread only (single-producer/single-consumer ring).
#ifndef SPATIALEQ_DSP_H
#define SPATIALEQ_DSP_H

#ifdef __APPLE__
#include <CoreAudio/CoreAudioTypes.h>
#define SQ_HAS_COREAUDIO 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SQ_MAX_BANDS 16
#define SQ_MAX_SOURCES 8

typedef enum {
    SQ_BAND_PEAK = 0,
    SQ_BAND_LOWSHELF = 1,
    SQ_BAND_HIGHSHELF = 2,
    SQ_BAND_LOWPASS = 3,
    SQ_BAND_HIGHPASS = 4
} sq_band_type;

typedef enum {
    SQ_MODE_HEADPHONES = 0,
    SQ_MODE_SPEAKERS = 1
} sq_output_mode;

typedef enum {
    SQ_UPMIX_STEREO = 0, // sources: L R
    SQ_UPMIX_5_1 = 1,    // sources: L R C Ls Rs
    SQ_UPMIX_7_1 = 2     // sources: L R C Ls Rs Lb Rb
} sq_upmix;

typedef struct {
    int enabled;
    int type;       // sq_band_type
    float freq;     // Hz
    float gainDb;
    float q;
} sq_band;

typedef struct {
    float azimuthDeg;   // 0 = front, +90 = right, +-180 = behind
    float elevationDeg; // +90 = above
    float distance;     // metres, 0.3 .. 5
    float gain;         // linear
} sq_source;

typedef struct {
    int enabled;            // 0 = bypass (crossfaded)
    float preampDb;

    int eqEnabled;
    int numBands;
    sq_band bands[SQ_MAX_BANDS];

    int mode;               // sq_output_mode

    // Headphones: binaural virtualisation
    int spatialEnabled;
    int upmix;              // sq_upmix
    sq_source sources[SQ_MAX_SOURCES];

    // Speakers: widening + crosstalk cancellation
    float width;            // 0 = mono, 1 = unchanged, 2 = extra wide
    int xtcEnabled;
    float xtcStrength;      // 0 .. 1
    float speakerSpanDeg;   // angle between the two speakers seen from the listener

    // Room / reverb (both modes)
    float roomSize;         // 0 .. 1
    float reverbMix;        // 0 .. 1

    // Extras
    float bassBoostDb;      // 0 .. 12
    float dialogueBoost;    // 0 .. 1
    int levelerEnabled;
    float levelerTargetDb;  // RMS dBFS target, e.g. -20
    int limiterEnabled;
    float limiterCeilingDb; // e.g. -1
    float outputGainDb;
} sq_params;

typedef struct {
    float peakL;            // linear, post-processing
    float peakR;
    float limiterReductionDb;
    float levelerGainDb;
} sq_meters;

typedef struct sq_engine sq_engine;

void sq_params_default(sq_params* p);

sq_engine* sq_engine_create(void);
void sq_engine_destroy(sq_engine* e);

void sq_engine_prepare(sq_engine* e, double sampleRate, int maxFrames);
void sq_engine_reset(sq_engine* e);

void sq_engine_set_params(sq_engine* e, const sq_params* p);
void sq_engine_set_head_yaw(sq_engine* e, float yawDeg); // + = head turned to the right

// In-place planar stereo processing.
void sq_engine_process(sq_engine* e, float* left, float* right, int frames);

// Interleaved processing: reads `inChannels`-channel input (first two channels used),
// writes `outChannels`-channel output (channels 0/1 processed, the rest zeroed). in may equal out
// only when inChannels == outChannels.
void sq_engine_process_interleaved(sq_engine* e, const float* in, int inChannels, float* out, int outChannels,
                                   int frames);

#ifdef SQ_HAS_COREAUDIO
// Reads Float32 audio from `in` (skipping the first `inChannelOffset` channels), processes it
// and writes channels 0/1 of `out`; any further output channels are zeroed.
void sq_engine_process_abl(sq_engine* e, const AudioBufferList* in, int inChannelOffset,
                           AudioBufferList* out);
#endif

// Pulls up to maxSamples of mono post-processing audio for visualisation. Returns count.
int sq_engine_read_analysis(sq_engine* e, float* dst, int maxSamples);

void sq_engine_get_meters(sq_engine* e, sq_meters* out);

// Magnitude response (dB) of preamp + EQ bands at the given frequencies, for drawing curves.
void sq_eq_response(const sq_params* p, double sampleRate, const float* freqs, float* outDb, int n);

#ifdef __cplusplus
}
#endif

#endif
