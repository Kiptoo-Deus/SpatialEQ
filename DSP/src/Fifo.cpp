#include "spatialeq_fifo.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

struct sq_fifo {
    std::vector<float> l, r;
    uint32_t mask = 0;
    std::atomic<uint32_t> head{0}; // written by producer
    std::atomic<uint32_t> tail{0}; // written by consumer
    std::atomic<int> flush{0};
    std::atomic<int64_t> consumed{0};
};

extern "C" {

sq_fifo* sq_fifo_create(int capacityFrames) {
    auto* f = new sq_fifo();
    uint32_t size = 1;
    while (size < uint32_t(std::max(capacityFrames, 16))) size <<= 1;
    f->l.assign(size, 0.0f);
    f->r.assign(size, 0.0f);
    f->mask = size - 1;
    return f;
}

void sq_fifo_destroy(sq_fifo* f) { delete f; }

int sq_fifo_free_frames(const sq_fifo* f) {
    return int(f->mask + 1 - (f->head.load(std::memory_order_relaxed) - f->tail.load(std::memory_order_acquire)));
}

int sq_fifo_available_frames(const sq_fifo* f) {
    return int(f->head.load(std::memory_order_acquire) - f->tail.load(std::memory_order_relaxed));
}

int sq_fifo_write(sq_fifo* f, const float* left, const float* right, int frames) {
    if (f->flush.load(std::memory_order_acquire)) return 0; // wait until the consumer has flushed
    const uint32_t head = f->head.load(std::memory_order_relaxed);
    const int n = std::min(frames, sq_fifo_free_frames(f));
    for (int i = 0; i < n; ++i) {
        f->l[(head + i) & f->mask] = left[i];
        f->r[(head + i) & f->mask] = right ? right[i] : left[i];
    }
    f->head.store(head + n, std::memory_order_release);
    return n;
}

void sq_fifo_request_flush(sq_fifo* f) { f->flush.store(1, std::memory_order_release); }
int sq_fifo_flush_pending(const sq_fifo* f) { return f->flush.load(std::memory_order_acquire); }

void sq_fifo_reset(sq_fifo* f) {
    f->tail.store(f->head.load(std::memory_order_acquire), std::memory_order_release);
    f->consumed.store(0, std::memory_order_relaxed);
    f->flush.store(0, std::memory_order_release);
}

int sq_fifo_read(sq_fifo* f, float* left, float* right, int frames) {
    uint32_t tail = f->tail.load(std::memory_order_relaxed);
    const uint32_t head = f->head.load(std::memory_order_acquire);
    if (f->flush.load(std::memory_order_acquire)) {
        f->tail.store(head, std::memory_order_release);
        f->consumed.store(0, std::memory_order_relaxed);
        f->flush.store(0, std::memory_order_release);
        return 0;
    }
    const int n = std::min<int>(frames, int(head - tail));
    for (int i = 0; i < n; ++i) {
        left[i] = f->l[(tail + i) & f->mask];
        right[i] = f->r[(tail + i) & f->mask];
    }
    f->tail.store(tail + n, std::memory_order_release);
    f->consumed.fetch_add(n, std::memory_order_relaxed);
    return n;
}

int64_t sq_fifo_frames_consumed(const sq_fifo* f) { return f->consumed.load(std::memory_order_relaxed); }

} // extern "C"
