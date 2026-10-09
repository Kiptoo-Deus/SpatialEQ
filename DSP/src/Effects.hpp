// Room reverb, bass / dialogue enhancement and dynamics.
#pragma once

#include "Primitives.hpp"

namespace sq {

// ---------------------------------------------------------------------------------------------
// 4-line feedback delay network with a Householder matrix and in-loop damping.

class RoomReverb {
public:
    void prepare(double fs) {
        fs_ = fs;
        const double maxScale = 1.6;
        for (int i = 0; i < 4; ++i) lines_[i].allocate(int(kBase[i] * maxScale * fs / 44100.0) + 8);
        predelay_.allocate(int(0.05 * fs) + 8);
        configure(0.4f);
        reset();
    }
    void reset() {
        for (auto& l : lines_) l.reset();
        for (auto& d : damp_) d.reset();
        predelay_.reset();
    }

    void configure(float roomSize) {
        roomSize = std::clamp(roomSize, 0.0f, 1.0f);
        const double scale = 0.35 + 1.2 * roomSize;
        const double rt60 = 0.25 + 2.0 * roomSize;
        for (int i = 0; i < 4; ++i) {
            delay_[i] = int(kBase[i] * scale * fs_ / 44100.0);
            fbGain_[i] = float(std::pow(10.0, -3.0 * delay_[i] / (fs_ * rt60)));
            damp_[i].setLowPass(fs_, 9000.0 - 5000.0 * roomSize);
        }
        predelaySamples_ = int((0.008 + 0.025 * roomSize) * fs_);
    }

    // Adds wet signal into outL / outR from the stereo send.
    void process(const float* inL, const float* inR, float* outL, float* outR, int frames, float wet) {
        for (int n = 0; n < frames; ++n) {
            predelay_.push(0.5f * (inL[n] + inR[n]));
            const float x = predelay_.readInt(predelaySamples_);

            float s[4];
            for (int i = 0; i < 4; ++i) s[i] = damp_[i].process(lines_[i].readInt(delay_[i] - 1)) * fbGain_[i];
            const float sum = 0.5f * (s[0] + s[1] + s[2] + s[3]); // Householder: I - 2/N * 11^T
            for (int i = 0; i < 4; ++i) lines_[i].push(s[i] - sum + (i & 1 ? -x : x) * 0.5f);

            outL[n] += wet * (s[0] + 0.6f * s[2]);
            outR[n] += wet * (s[1] + 0.6f * s[3]);
        }
    }

private:
    static constexpr int kBase[4] = {1557, 1617, 1491, 1422};
    double fs_ = 48000;
    DelayLine lines_[4];
    DelayLine predelay_;
    OnePole damp_[4];
    int delay_[4]{};
    float fbGain_[4]{};
    int predelaySamples_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Bass enhancer: low shelf plus "missing fundamental" harmonics generated from the sub band,
// which makes bass audible on small speakers and earbuds.

class BassEnhancer {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (auto& f : sub_) f.c = designBiquad(BandType::LowPass, fs, 120.0, 0, 0.707);
        for (auto& f : hp_) f.c = designBiquad(BandType::HighPass, fs, 150.0, 0, 0.707);
        for (auto& f : lp_) f.c = designBiquad(BandType::LowPass, fs, 450.0, 0, 0.707);
        configure(0);
        reset();
    }
    void reset() {
        for (auto* arr : {sub_, hp_, lp_, shelf_}) for (int i = 0; i < 2; ++i) arr[i].reset();
    }
    void configure(float boostDb) {
        boostDb = std::clamp(boostDb, 0.0f, 12.0f);
        active_ = boostDb > 0.05f;
        for (auto& f : shelf_) f.c = designBiquad(BandType::LowShelf, fs_, 100.0, boostDb, 0.7);
        harmonics_ = boostDb / 12.0f * 0.35f;
    }
    void process(float* l, float* r, int frames) {
        if (!active_) return;
        float* ch[2] = {l, r};
        for (int c = 0; c < 2; ++c) {
            for (int n = 0; n < frames; ++n) {
                const float x = ch[c][n];
                const float h = lp_[c].process(hp_[c].process(std::tanh(4.0f * sub_[c].process(x))));
                ch[c][n] = shelf_[c].process(x) + harmonics_ * h;
            }
        }
    }

private:
    double fs_ = 48000;
    bool active_ = false;
    float harmonics_ = 0;
    Biquad sub_[2], hp_[2], lp_[2], shelf_[2];
};

// ---------------------------------------------------------------------------------------------
// Dialogue boost: lift the presence band of the centre (mid) signal and pull the sides back.

class DialogueEnhancer {
public:
    void prepare(double fs) { fs_ = fs; configure(0); presence_.reset(); }
    void configure(float amount) {
        amount_ = std::clamp(amount, 0.0f, 1.0f);
        presence_.c = designBiquad(BandType::Peak, fs_, 2500.0, 9.0 * amount_, 0.8);
    }
    void process(float* l, float* r, int frames) {
        if (amount_ <= 0.001f) return;
        const float sideGain = 1.0f - 0.45f * amount_;
        for (int n = 0; n < frames; ++n) {
            const float mid = presence_.process(0.5f * (l[n] + r[n]));
            const float side = 0.5f * (l[n] - r[n]) * sideGain;
            l[n] = mid + side;
            r[n] = mid - side;
        }
    }

private:
    double fs_ = 48000;
    float amount_ = 0;
    Biquad presence_;
};

// ---------------------------------------------------------------------------------------------
// Volume leveler: slow RMS-based automatic gain towards a target loudness.

class Leveler {
public:
    void prepare(double fs) {
        fs_ = fs;
        rmsCoeff_ = float(std::exp(-1.0 / (fs * 0.4)));
        upCoeff_ = float(std::exp(-1.0 / (fs * 2.0)));
        downCoeff_ = float(std::exp(-1.0 / (fs * 0.25)));
        reset();
    }
    void reset() { ms_ = 0; gainDb_ = 0; }
    float currentGainDb() const { return gainDb_; }

    void process(float* l, float* r, int frames, float targetDb, bool enabled) {
        for (int n = 0; n < frames; ++n) {
            const float x2 = 0.5f * (l[n] * l[n] + r[n] * r[n]);
            ms_ = x2 + rmsCoeff_ * (ms_ - x2);
            const float levelDb = 10.0f * std::log10(ms_ + 1e-12f);

            float wantDb = enabled ? std::clamp(targetDb - levelDb, -12.0f, 12.0f) : 0.0f;
            if (enabled && levelDb < -60.0f) wantDb = gainDb_; // hold through silence
            const float c = wantDb > gainDb_ ? upCoeff_ : downCoeff_;
            gainDb_ = wantDb + c * (gainDb_ - wantDb);

            const float g = dbToGain(gainDb_);
            l[n] *= g;
            r[n] *= g;
        }
    }

private:
    double fs_ = 48000;
    float rmsCoeff_ = 0, upCoeff_ = 0, downCoeff_ = 0;
    float ms_ = 0, gainDb_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Stereo-linked look-ahead peak limiter with a final safety clip.

class Limiter {
public:
    void prepare(double fs) {
        lookahead_ = std::max(8, int(0.0015 * fs));
        for (auto& d : delay_) d.allocate(lookahead_ + 4);
        peaks_.allocate(lookahead_ + 4);
        attack_ = float(std::exp(-1.0 / (lookahead_ / 3.0)));
        release_ = float(std::exp(-1.0 / (fs * 0.08)));
        reset();
    }
    void reset() {
        for (auto& d : delay_) d.reset();
        peaks_.reset(1.0f); // unity gain needed until real peaks arrive
        gain_ = 1;
        minGain_ = 1;
    }
    int latency() const { return lookahead_; }

    // Returns the deepest gain reduction in dB over the block (<= 0).
    float process(float* l, float* r, int frames, float ceilingDb, bool enabled) {
        const float ceiling = dbToGain(ceilingDb);
        minGain_ = 1;
        for (int n = 0; n < frames; ++n) {
            const float peak = std::max(std::fabs(l[n]), std::fabs(r[n]));
            const float need = enabled && peak > ceiling ? ceiling / peak : 1.0f;
            peaks_.push(need);
            delay_[0].push(l[n]);
            delay_[1].push(r[n]);

            float target = 1.0f;
            for (int k = 0; k <= lookahead_; ++k) target = std::min(target, peaks_.readInt(k));
            const float c = target < gain_ ? attack_ : release_;
            gain_ = target + c * (gain_ - target);
            minGain_ = std::min(minGain_, gain_);

            float yl = delay_[0].readInt(lookahead_) * gain_;
            float yr = delay_[1].readInt(lookahead_) * gain_;
            if (enabled) {
                yl = std::clamp(yl, -ceiling, ceiling);
                yr = std::clamp(yr, -ceiling, ceiling);
            }
            l[n] = yl;
            r[n] = yr;
        }
        return gainToDb(minGain_);
    }

private:
    int lookahead_ = 64;
    DelayLine delay_[2];
    DelayLine peaks_;
    float attack_ = 0, release_ = 0;
    float gain_ = 1, minGain_ = 1;
};

} // namespace sq
