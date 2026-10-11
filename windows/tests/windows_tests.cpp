// Unit tests for the Windows app's model and audio plumbing (no audio hardware needed).
#include "AudioRouter.h"
#include "Settings.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace sq::app;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double tol = 1e-4) { return std::fabs(a - b) <= tol; }

SoundSettings roundTrip(const SoundSettings& s) {
    std::string text;
    writeSettings(s, text);
    SoundSettings out;
    beginRead(out);
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        const auto eq = line.find('=');
        if (eq != std::string::npos) readSettingsLine(out, line.substr(0, eq), line.substr(eq + 1));
    }
    finishRead(out);
    return out;
}

void testAutoEqParse() {
    const char* text =
        "Preamp: -6.2 dB\n"
        "Filter 1: ON LSC Fc 105 Hz Gain 6.9 dB Q 0.70\n"
        "Filter 2: ON PK Fc 2400 Hz Gain -3.1 dB Q 1.41\n"
        "Filter 3: OFF HSC Fc 10000 Hz Gain 2 dB Q 0.7\n"
        "Filter 4: ON XYZ Fc 100 Hz\n"
        "Filter 5: ON HPQ Fc 30 Hz\n";
    AutoEqResult r;
    check(parseAutoEq(text, r), "AutoEq parses");
    check(near(r.preampDb, -6.2), "AutoEq preamp");
    check(r.bands.size() == 4, "AutoEq band count");
    check(r.skipped == 1, "AutoEq skips unknown filter type");
    if (r.bands.size() == 4) {
        check(r.bands[0].type == BandType::LowShelf && near(r.bands[0].frequency, 105) && near(r.bands[0].gainDb, 6.9) &&
                  near(r.bands[0].q, 0.7),
              "AutoEq low shelf values");
        check(r.bands[1].type == BandType::Peak && near(r.bands[1].gainDb, -3.1), "AutoEq peak values");
        check(!r.bands[2].enabled, "AutoEq OFF filter is disabled");
        check(r.bands[3].type == BandType::HighPass && near(r.bands[3].q, 0.71), "AutoEq default Q for pass filters");
    }
    AutoEqResult none;
    check(!parseAutoEq("hello\nPreamp: 1 dB\n", none), "AutoEq rejects files without filters");
}

void testAutoEqRoundTrip() {
    SoundSettings s;
    s.preampDb = -3.5;
    s.bands[2].gainDb = 4.5;
    s.bands[9].enabled = false;
    AutoEqResult r;
    check(parseAutoEq(exportAutoEq(s), r), "export parses back");
    check(near(r.preampDb, -3.5), "export preamp");
    check(r.bands.size() == s.bands.size(), "export band count");
    for (size_t i = 0; i < r.bands.size() && i < s.bands.size(); ++i) {
        check(r.bands[i].type == s.bands[i].type, "export type");
        check(near(r.bands[i].gainDb, s.bands[i].gainDb, 0.05), "export gain");
        check(r.bands[i].enabled == s.bands[i].enabled, "export enabled");
    }
}

void testSettingsPersistence() {
    SoundSettings s;
    s.preampDb = -2.25;
    s.mode = OutputMode::Speakers;
    s.upmix = Upmix::Surround71;
    s.spatialEnabled = true;
    s.bands.resize(3);
    s.bands[1].type = BandType::HighShelf;
    s.bands[1].frequency = 8123.5;
    s.sources[4].azimuth = 97;
    s.sources[4].elevation = 20;
    s.sources[4].distance = 3.25;
    s.levelerEnabled = true;
    s.limiterCeilingDb = -0.5;
    const SoundSettings out = roundTrip(s);
    check(out == s, "settings survive a save/load round trip");
    check(out.bands.size() == 3, "band count persisted");
    check(near(out.sources[4].distance, 3.25), "source distance persisted");

    // Garbage and out-of-range values are clamped or ignored, never crash.
    SoundSettings g;
    beginRead(g);
    readSettingsLine(g, "band", "1,99,-5,500,0");
    readSettingsLine(g, "band", "not,a,band");
    readSettingsLine(g, "mode", "7");
    readSettingsLine(g, "preampDb", "nan");
    check(!readSettingsLine(g, "unknownKey", "1"), "unknown keys are reported");
    finishRead(g);
    check(g.bands.size() == 1 && g.bands[0].type == BandType::HighPass && g.bands[0].frequency >= 10 &&
              g.bands[0].gainDb <= 30 && g.bands[0].q >= 0.05,
          "bad band values are clamped");
    check(g.mode == OutputMode::Speakers, "enum values are clamped");
    check(g.sources.size() == size_t(SoundSettings::kSourceCount), "missing sources are filled in");
}

void testPresets() {
    const auto presets = builtInPresets();
    check(presets.size() == 12, "12 built-in presets, as on macOS / iOS / Android");
    check(presets.front().name == "Flat" && presets.front().settings == SoundSettings(), "Flat is the default");
    for (const auto& p : presets) {
        if (p.name == "Cinema")
            check(p.settings.spatialEnabled && p.settings.upmix == Upmix::Surround71 && near(p.settings.reverbMix, 0.25),
                  "Cinema preset values");
        if (p.name == "Laptop Speakers")
            check(p.settings.mode == OutputMode::Speakers && p.settings.bands[0].type == BandType::HighPass,
                  "Laptop Speakers preset values");
    }
}

void testParams() {
    SoundSettings s;
    s.bands.resize(20); // more than the engine supports
    s.width = 1.7;
    s.sources[2].azimuth = 12;
    const sq_params p = s.toParams(false);
    check(p.enabled == 0, "enabled flag");
    check(p.numBands == SQ_MAX_BANDS, "band count is capped");
    check(near(p.width, 1.7f) && near(p.sources[2].azimuthDeg, 12), "values copied");
    check(near(p.bands[0].q, 0.7f), "shelf Q matches the Swift ten-band layout");
}

void testDeinterleave() {
    float l[3], r[3];
    const float f[6] = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f, -0.6f};
    check(deinterleave(f, {2, 32, 32, true}, 3, l, r) && l[2] == 0.5f && r[1] == -0.4f, "float stereo");

    const float f6[12] = {1, 2, 9, 9, 9, 9, 3, 4, 9, 9, 9, 9};
    check(deinterleave(f6, {6, 32, 32, true}, 2, l, r) && l[1] == 3 && r[1] == 4, "first two of 5.1");

    const float mono[2] = {0.25f, -0.75f};
    check(deinterleave(mono, {1, 32, 32, true}, 2, l, r) && r[0] == 0.25f && r[1] == -0.75f, "mono is duplicated");

    const int16_t s16[4] = {16384, -32768, 0, 32767};
    check(deinterleave(s16, {2, 16, 16, false}, 2, l, r) && near(l[0], 0.5) && near(r[0], -1.0), "int16");

    const uint8_t s24[6] = {0x00, 0x00, 0x40, 0x00, 0x00, 0xC0}; // +0.5, -0.5
    check(deinterleave(s24, {2, 24, 24, false}, 1, l, r) && near(l[0], 0.5) && near(r[0], -0.5), "int24");

    const int32_t s32[2] = {1 << 30, -(1 << 30)};
    check(deinterleave(s32, {2, 32, 32, false}, 1, l, r) && near(l[0], 0.5) && near(r[0], -0.5), "int32");

    check(!deinterleave(f, {2, 64, 64, true}, 1, l, r), "double is rejected");
}

void testStereoRing() {
    StereoRing ring;
    ring.allocate(8);
    std::vector<float> l = {1, 2, 3, 4, 5, 6}, r = {-1, -2, -3, -4, -5, -6};
    check(ring.write(l.data(), r.data(), 6) == 6, "ring write");
    check(ring.write(l.data(), r.data(), 6) == 2, "ring write stops when full");
    ring.trimTo(3);
    check(ring.available() == 3, "trim keeps the newest frames");
    float out[4 * 3];
    check(ring.readInterleaved(out, 4, 3) == 3, "ring read");
    // After trimming, the newest three frames written were 6, 1, 2 (the second write wrapped).
    check(out[0] == 6 && out[1] == -6 && out[2] == 0 && out[3] == 0, "4-channel output zeroes channels 3 and 4");
    check(out[4] == 1 && out[8] == 2, "ring keeps order across the wrap");
    check(ring.available() == 0, "ring empty after read");
}

void testDuckCompensation() {
    DuckCompensation comp;
    const double rate = 48000;
    std::vector<float> l(480), r(480);

    // Ducked audio (an app at full scale, turned down to kDuckLevel) is restored to full level.
    for (size_t i = 0; i < l.size(); ++i) l[i] = r[i] = kDuckLevel * std::sin(float(i) * 0.1f);
    check(near(comp.gainFor(l.data(), r.data(), 480, rate), 1.0 / kDuckLevel, 1e-3), "ducked audio is compensated");

    // An app that is not ducked yet must never get the 80 dB boost...
    std::vector<float> loud(480, 0.3f);
    check(comp.gainFor(loud.data(), loud.data(), 480, rate) == 1.0f, "un-ducked audio passes at unity");
    // ...and the guard holds for a moment after it goes quiet, so an app that pauses briefly is safe.
    check(comp.gainFor(l.data(), r.data(), 480, rate) == 1.0f, "guard holds after un-ducked audio");
    for (int i = 0; i < 40; ++i) comp.gainFor(l.data(), r.data(), 480, rate); // 0.4 s of ducked-only audio
    check(near(comp.gainFor(l.data(), r.data(), 480, rate), 1.0 / kDuckLevel, 1e-3), "guard releases after 0.3 s");

    // Even the worst case below the guard threshold stays within the limiter's reach (+12 dBFS).
    std::vector<float> edge(480, DuckCompensation::kOverThreshold);
    check(edge[0] * comp.gainFor(edge.data(), edge.data(), 480, rate) <= 4.01f, "boost is bounded below the threshold");
}

void testLooksDucked() {
    check(looksDucked(kDuckLevel, 1.0f), "full-volume app at duck level");
    check(looksDucked(0.5f * kDuckLevel, 0.5f), "half-volume app at duck level");
    check(!looksDucked(0.5f, 1.0f), "user raised the volume");
    check(!looksDucked(0.01f, 1.0f), "1 % is a user setting, not our duck");
}

} // namespace

int main() {
    testAutoEqParse();
    testAutoEqRoundTrip();
    testSettingsPersistence();
    testPresets();
    testParams();
    testDeinterleave();
    testStereoRing();
    testDuckCompensation();
    testLooksDucked();
    if (failures) {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("All Windows app tests passed\n");
    return 0;
}
