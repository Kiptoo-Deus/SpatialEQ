// Spatial processing: stereo upmix, binaural headphone rendering and speaker crosstalk cancellation.
#pragma once

#include "Primitives.hpp"

namespace sq {

constexpr int kMaxSources = 8;

// ---------------------------------------------------------------------------------------------
// Passive matrix upmix from stereo to 2 / 5 / 7 virtual channels.
// Surround feeds are the band-limited, delayed side signal so they stay decorrelated from the fronts.

class Upmixer {
public:
    void prepare(double fs, int maxFrames) {
        fs_ = fs;
        const int maxDelay = int(0.04 * fs) + 4;
        for (auto& d : delays_) d.allocate(maxDelay);
        sideLp_.setLowPass(fs, 7000.0);
        sideHp_.setHighPass(fs, 100.0);
        for (auto& ch : out_) ch.assign(maxFrames, 0.0f);
        reset();
    }
    void reset() {
        for (auto& d : delays_) d.reset();
        sideLp_.reset();
        sideHp_.reset();
    }

    // Returns the number of channels written; pointers stay valid until the next call.
    int process(int layout, const float* l, const float* r, int frames, float* const** channelsOut) {
        for (int i = 0; i < kMaxSources; ++i) ptrs_[i] = out_[i].data();
        *channelsOut = ptrs_;

        if (layout == 0) {
            std::memcpy(ptrs_[0], l, sizeof(float) * frames);
            std::memcpy(ptrs_[1], r, sizeof(float) * frames);
            return 2;
        }

        const float dLs = float(0.012 * fs_), dRs = float(0.015 * fs_);
        const float dLb = float(0.022 * fs_), dRb = float(0.026 * fs_);
        for (int n = 0; n < frames; ++n) {
            const float c = 0.35f * (l[n] + r[n]);
            const float side = sideLp_.process(sideHp_.process(0.5f * (l[n] - r[n])));
            delays_[0].push(side + 0.25f * l[n]);
            delays_[1].push(-side + 0.25f * r[n]);

            ptrs_[0][n] = 0.8f * (l[n] - 0.5f * c);
            ptrs_[1][n] = 0.8f * (r[n] - 0.5f * c);
            ptrs_[2][n] = 0.8f * c;
            ptrs_[3][n] = 0.7f * delays_[0].read(dLs);
            ptrs_[4][n] = 0.7f * delays_[1].read(dRs);
            if (layout == 2) {
                ptrs_[5][n] = 0.55f * delays_[0].read(dLb);
                ptrs_[6][n] = 0.55f * delays_[1].read(dRb);
            }
        }
        return layout == 1 ? 5 : 7;
    }

private:
    double fs_ = 48000;
    DelayLine delays_[2];
    OnePole sideLp_, sideHp_;
    std::vector<float> out_[kMaxSources];
    float* ptrs_[kMaxSources]{};
};

// ---------------------------------------------------------------------------------------------
// Binaural renderer using the Brown & Duda structural HRTF model:
//   - interaural time difference from a spherical head,
//   - first-order head-shadow filter per ear,
//   - a tone shelf for front/back and elevation cues.

struct SourcePosition {
    float azimuthDeg = 0, elevationDeg = 0, distance = 1, gain = 1;
};

class BinauralRenderer {
public:
    static constexpr double kHeadRadius = 0.0875; // metres
    static constexpr double kSpeedOfSound = 343.0;

    void prepare(double fs) {
        fs_ = fs;
        for (auto& s : src_) {
            s.line.allocate(int(fs * 0.002) + 8);
            for (auto& e : s.ear) {
                e.delay.setTime(fs, 0.02);
                e.gain.setTime(fs, 0.02);
            }
        }
        reset();
        for (int i = 0; i < kMaxSources; ++i) updateSource(i, true);
    }

    void reset() {
        for (auto& s : src_) {
            s.line.reset();
            s.tone.reset();
            for (auto& e : s.ear) e.shadow.reset();
        }
    }

    void setPosition(int i, const SourcePosition& p) { src_[i].pos = p; }
    void setHeadYaw(float deg) { headYawDeg_ = deg; }

    // Recomputes filters/targets from the current positions. Call once per block.
    void update() {
        for (int i = 0; i < kMaxSources; ++i) updateSource(i, false);
    }

    // Adds the binaural render of `numSources` mono inputs into outL / outR.
    void process(const float* const* in, int numSources, float* outL, float* outR, int frames) {
        for (int s = 0; s < numSources; ++s) {
            Source& src = src_[s];
            const float* x = in[s];
            for (int n = 0; n < frames; ++n) {
                src.line.push(src.tone.process(x[n]));
                for (int e = 0; e < 2; ++e) {
                    Ear& ear = src.ear[e];
                    const float v = ear.shadow.process(src.line.read(ear.delay.next())) * ear.gain.next();
                    (e == 0 ? outL : outR)[n] += v;
                }
            }
        }
    }

private:
    struct Ear {
        OnePole shadow;
        Smoother delay, gain;
    };
    struct Source {
        SourcePosition pos;
        DelayLine line;
        Biquad tone;
        Ear ear[2];
    };

    void updateSource(int i, bool snap) {
        Source& s = src_[i];
        const double az = (s.pos.azimuthDeg - headYawDeg_) * kPi / 180.0;
        const double el = std::clamp<double>(s.pos.elevationDeg, -80.0, 80.0) * kPi / 180.0;
        // Listener frame: x right, y front, z up.
        const double x = std::sin(az) * std::cos(el);
        const double y = std::cos(az) * std::cos(el);
        const double z = std::sin(el);

        const double dist = std::clamp<double>(s.pos.distance, 0.3, 5.0);
        const float distGain = float(std::min(1.0 / dist, 2.0)) * s.pos.gain;

        // Front/back and elevation colouration.
        const double toneDb = -5.0 * std::max(0.0, -y) + 2.5 * z;
        s.tone.c = designBiquad(BandType::HighShelf, fs_, 6000.0, toneDb, 0.7);

        const double w0 = kSpeedOfSound / kHeadRadius;
        const double K = 2.0 * fs_;
        for (int e = 0; e < 2; ++e) {
            const double cosTheta = (e == 0 ? -x : x); // angle between source and this ear's axis
            const double theta = std::acos(std::clamp(cosTheta, -1.0, 1.0));

            // Head shadow: H(s) = (alpha*s + 2*w0) / (s + 2*w0), bilinear transformed.
            constexpr double alphaMin = 0.1, thetaMin = 150.0 * kPi / 180.0;
            const double alpha =
                (1.0 + alphaMin / 2.0) + (1.0 - alphaMin / 2.0) * std::cos(std::min(theta / thetaMin, 1.0) * kPi);
            const double norm = 2.0 * w0 + K;
            Ear& ear = s.ear[e];
            ear.shadow.b0 = float((2.0 * w0 + alpha * K) / norm);
            ear.shadow.b1 = float((2.0 * w0 - alpha * K) / norm);
            ear.shadow.a1 = float((2.0 * w0 - K) / norm);

            // ITD from a rigid sphere, offset so the nearest ear has ~0 delay.
            const double t = theta < kPi / 2 ? (1.0 - std::cos(theta)) : (1.0 + theta - kPi / 2);
            const float delaySamples = float(t * kHeadRadius / kSpeedOfSound * fs_);

            ear.delay.target = delaySamples;
            ear.gain.target = distGain;
            if (snap) {
                ear.delay.snap(delaySamples);
                ear.gain.snap(distGain);
            }
        }
    }

    double fs_ = 48000;
    float headYawDeg_ = 0;
    Source src_[kMaxSources];
};

// ---------------------------------------------------------------------------------------------
// Speaker mode: mid/side widening, then recursive crosstalk cancellation (Atal-Schroeder style)
// restricted to 250 Hz - 6 kHz so the loop never boosts the bass.

class SpeakerProcessor {
public:
    void prepare(double fs) {
        fs_ = fs;
        for (auto& d : fb_) d.allocate(int(fs * 0.002) + 8);
        for (int c = 0; c < 2; ++c) {
            hp_[c].setHighPass(fs, 250.0);
            lp_[c].setLowPass(fs, 6000.0);
        }
        width_.setTime(fs, 0.03);
        width_.snap(1.0f);
        reset();
    }
    void reset() {
        for (auto& d : fb_) d.reset();
        for (int c = 0; c < 2; ++c) { hp_[c].reset(); lp_[c].reset(); }
    }

    void configure(float width, bool xtc, float strength, float spanDeg) {
        width_.target = std::clamp(width, 0.0f, 2.5f);
        xtc_ = xtc;
        g_ = 0.85f * std::clamp(strength, 0.0f, 1.0f);
        const double half = std::clamp<double>(spanDeg, 5.0, 90.0) * 0.5 * kPi / 180.0;
        // Extra path length to the far ear (spherical head).
        delay_ = float((BinauralRenderer::kHeadRadius / BinauralRenderer::kSpeedOfSound) *
                       (half + std::sin(half)) * fs_);
        delay_ = std::max(delay_, 1.0f);
    }

    void process(float* l, float* r, int frames) {
        for (int n = 0; n < frames; ++n) {
            const float w = width_.next();
            const float mid = 0.5f * (l[n] + r[n]);
            const float side = 0.5f * (l[n] - r[n]) * w;
            float xl = mid + side, xr = mid - side;

            if (xtc_) {
                const float cl = lp_[0].process(hp_[0].process(fb_[1].read(delay_)));
                const float cr = lp_[1].process(hp_[1].process(fb_[0].read(delay_)));
                xl -= g_ * cl;
                xr -= g_ * cr;
            }
            fb_[0].push(xl);
            fb_[1].push(xr);
            l[n] = xl;
            r[n] = xr;
        }
    }

private:
    double fs_ = 48000;
    DelayLine fb_[2];
    OnePole hp_[2], lp_[2];
    Smoother width_;
    bool xtc_ = false;
    float g_ = 0, delay_ = 1;
};

} // namespace sq
