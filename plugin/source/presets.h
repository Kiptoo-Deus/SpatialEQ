// Built-in presets (the same 12 as the SpatialEQ apps) and AutoEq / Equalizer APO text import/export.
#pragma once

#include "params.h"

#include <string>
#include <vector>

namespace sq {

struct Preset {
    std::string name;
    Model model;
};

const std::vector<Preset>& builtInPresets();

/// Parses AutoEq / Equalizer APO "ParametricEQ.txt" text into the bands and preamp of `base` (other
/// settings are kept). Returns false when the text contains no filters.
bool importParametricEQ(const std::string& text, const Model& base, Model& out, int& imported, int& skipped);

/// Writes the active bands and the preamp in the same format.
std::string exportParametricEQ(const Model& m);

} // namespace sq
