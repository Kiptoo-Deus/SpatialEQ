// Sound settings, presets and AutoEq import/export for the Windows app.
// Mirrors shared/Swift/Model (macOS / iOS) and android/.../model so presets behave the same everywhere.
#pragma once

#include "spatialeq_dsp.h"

#include <string>
#include <vector>

namespace sq::app {

enum class BandType { Peak = 0, LowShelf, HighShelf, LowPass, HighPass };
enum class OutputMode { Headphones = 0, Speakers };
enum class Upmix { Stereo = 0, Surround51, Surround71 };

const char* label(BandType t);
const char* label(Upmix u);
bool hasGain(BandType t);
// Channel names for the virtual sources of an upmix layout ("L", "R", "C", ...).
std::vector<const char*> channelNames(Upmix u);

struct EqBand {
    bool enabled = true;
    BandType type = BandType::Peak;
    double frequency = 1000;
    double gainDb = 0;
    double q = 1.0;
};

struct VirtualSource {
    double azimuth = 0; // degrees, 0 front, + right
    double elevation = 0;
    double distance = 1.5;
    double gain = 1;
};

struct SoundSettings {
    static constexpr int kMaxBands = SQ_MAX_BANDS;
    static constexpr int kSourceCount = SQ_MAX_SOURCES;

    double preampDb = 0;
    bool eqEnabled = true;
    std::vector<EqBand> bands = tenBand();

    OutputMode mode = OutputMode::Headphones;
    bool spatialEnabled = false;
    Upmix upmix = Upmix::Stereo;
    std::vector<VirtualSource> sources = defaultSources();

    double width = 1;
    bool crosstalkEnabled = false;
    double crosstalkStrength = 0.6;
    double speakerSpan = 30;

    double roomSize = 0.35;
    double reverbMix = 0;

    double bassBoostDb = 0;
    double dialogueBoost = 0;
    bool levelerEnabled = false;
    double levelerTargetDb = -20;
    bool limiterEnabled = true;
    double limiterCeilingDb = -1;
    double outputGainDb = 0;

    static std::vector<EqBand> tenBand();
    static std::vector<VirtualSource> defaultSources();
    void resetSources() { sources = defaultSources(); }

    sq_params toParams(bool enabled) const;

    bool operator==(const SoundSettings& o) const;
    bool operator!=(const SoundSettings& o) const { return !(*this == o); }
};

struct Preset {
    std::string name;
    SoundSettings settings;
    bool builtIn = false;
};

std::vector<Preset> builtInPresets();

// ---- Persistence: a plain "key=value" text format, one setting per line. ----

// Appends the settings to `out` as "key=value" lines. Bands and sources repeat as "band=" / "source=".
void writeSettings(const SoundSettings& s, std::string& out);
// Reading a block: beginRead clears the repeated lists, readSettingsLine applies one line (false if the
// key is not a settings key), finishRead fills in anything missing (e.g. a file with no sources).
void beginRead(SoundSettings& s);
bool readSettingsLine(SoundSettings& s, const std::string& key, const std::string& value);
void finishRead(SoundSettings& s);

// ---- AutoEq / Equalizer APO "ParametricEQ.txt" ----

struct AutoEqResult {
    double preampDb = 0;
    std::vector<EqBand> bands;
    int skipped = 0;
};

// Returns false (and leaves `out` empty) if the text has no parametric filters.
bool parseAutoEq(const std::string& text, AutoEqResult& out);
std::string exportAutoEq(const SoundSettings& s);

std::string formatHz(double f);

} // namespace sq::app
