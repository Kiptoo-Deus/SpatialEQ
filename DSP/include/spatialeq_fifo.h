// Lock-free stereo FIFO between a decoder thread (producer) and the audio thread (consumer),
// used by the player apps (iOS, Android) to stream decoded files into the engine.
#ifndef SPATIALEQ_FIFO_H
#define SPATIALEQ_FIFO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sq_fifo sq_fifo;

sq_fifo* sq_fifo_create(int capacityFrames);
void sq_fifo_destroy(sq_fifo* f);

// Producer side.
int sq_fifo_write(sq_fifo* f, const float* left, const float* right, int frames); // returns frames written
int sq_fifo_free_frames(const sq_fifo* f);
// Asks the consumer to discard everything buffered (e.g. on seek). Poll sq_fifo_flush_pending until 0.
void sq_fifo_request_flush(sq_fifo* f);
int sq_fifo_flush_pending(const sq_fifo* f);
// Only when no consumer is running (audio stopped): empties the FIFO immediately.
void sq_fifo_reset(sq_fifo* f);

// Consumer (audio thread) side. Returns frames read; never blocks.
int sq_fifo_read(sq_fifo* f, float* left, float* right, int frames);
int sq_fifo_available_frames(const sq_fifo* f);

// Total frames delivered to the consumer since the last reset/flush (for the playback position).
int64_t sq_fifo_frames_consumed(const sq_fifo* f);

#ifdef __cplusplus
}
#endif

#endif
