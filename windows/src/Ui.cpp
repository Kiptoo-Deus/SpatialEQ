#include "Ui.h"

#include "App.h"

#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace sq::app {

namespace theme {
ImU32 channelColor(int index, float alpha) {
    // Same palette as SceneTheme.channelColors (shared/Swift/UI/SceneController.swift).
    static const float kColors[8][3] = {
        {0.30f, 0.70f, 1.00f}, {1.00f, 0.40f, 0.45f}, {0.95f, 0.95f, 0.95f}, {0.55f, 0.45f, 1.00f},
        {1.00f, 0.55f, 0.85f}, {0.35f, 0.95f, 0.65f}, {1.00f, 0.80f, 0.30f}, {0.70f, 0.70f, 0.70f},
    };
    const float* c = kColors[std::clamp(index, 0, 7)];
    return rgba(c[0], c[1], c[2], alpha);
}
} // namespace theme

void setupStyle(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsDark(&s);
    s.WindowRounding = 0;
    s.ChildRounding = 12;
    s.FrameRounding = 6;
    s.PopupRounding = 8;
    s.GrabRounding = 6;
    s.ScrollbarRounding = 6;
    s.TabRounding = 6;
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.PopupBorderSize = 1;
    s.FrameBorderSize = 0;
    s.WindowPadding = ImVec2(12, 12);
    s.FramePadding = ImVec2(8, 5);
    s.ItemSpacing = ImVec2(8, 7);
    s.ItemInnerSpacing = ImVec2(6, 4);
    s.GrabMinSize = 12;
    s.ScrollbarSize = 10;

    ImVec4* c = s.Colors;
    const ImVec4 a = theme::accent;
    c[ImGuiCol_WindowBg] = theme::background;
    c[ImGuiCol_ChildBg] = theme::panel;
    c[ImGuiCol_PopupBg] = ImVec4(0.09f, 0.10f, 0.14f, 0.98f);
    c[ImGuiCol_Border] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_Text] = ImVec4(0.93f, 0.95f, 0.97f, 1);
    c[ImGuiCol_TextDisabled] = theme::textDim;
    c[ImGuiCol_FrameBg] = ImVec4(1, 1, 1, 0.06f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(1, 1, 1, 0.10f);
    c[ImGuiCol_FrameBgActive] = ImVec4(1, 1, 1, 0.14f);
    c[ImGuiCol_Button] = ImVec4(1, 1, 1, 0.07f);
    c[ImGuiCol_ButtonHovered] = ImVec4(1, 1, 1, 0.13f);
    c[ImGuiCol_ButtonActive] = ImVec4(a.x, a.y, a.z, 0.35f);
    c[ImGuiCol_Header] = ImVec4(a.x, a.y, a.z, 0.18f);
    c[ImGuiCol_HeaderHovered] = ImVec4(a.x, a.y, a.z, 0.28f);
    c[ImGuiCol_HeaderActive] = ImVec4(a.x, a.y, a.z, 0.38f);
    c[ImGuiCol_SliderGrab] = a;
    c[ImGuiCol_SliderGrabActive] = ImVec4(0.6f, 0.95f, 1.0f, 1);
    c[ImGuiCol_CheckMark] = a;
    c[ImGuiCol_Separator] = ImVec4(1, 1, 1, 0.08f);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = ImVec4(1, 1, 1, 0.12f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(1, 1, 1, 0.2f);
    c[ImGuiCol_ScrollbarGrabActive] = ImVec4(1, 1, 1, 0.28f);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.5f);
    s.ScaleAllSizes(scale);
}

namespace {

// Right edge of the current window's content region, in window coordinates.
float regionRight() { return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; }

// ---------------------------------------------------------------------------------------------
// Widgets

bool toggle(const char* id, bool* v, float scale) {
    const float h = 18 * scale, w = 32 * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    const bool clicked = ImGui::IsItemClicked();
    if (clicked) *v = !*v;
    const float t = *v ? 1.0f : 0.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = *v ? ImGui::GetColorU32(theme::accent) : ImGui::GetColorU32(ImVec4(1, 1, 1, ImGui::IsItemHovered() ? 0.2f : 0.14f));
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), bg, h * 0.5f);
    const float r = h * 0.5f - 2 * scale;
    dl->AddCircleFilled(ImVec2(p.x + h * 0.5f + t * (w - h), p.y + h * 0.5f), r,
                        *v ? theme::rgba(0.03f, 0.05f, 0.08f) : theme::rgba(0.9f, 0.92f, 0.95f));
    return clicked;
}

// "Label ........ [switch]" row.
bool toggleRow(const char* label, bool* v, float scale) {
    ImGui::PushID(label);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(regionRight() - 32 * scale);
    const bool changed = toggle("##t", v, scale);
    ImGui::PopID();
    return changed;
}

using Formatter = std::function<std::string(double)>;

std::string fmt(const char* f, double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

// Thin track with a round knob. Logarithmic mapping for frequency / Q. Double-click resets to
// `reset` when one is given.
bool slimSlider(const char* id, double* v, double lo, double hi, float width, bool logarithmic,
                const double* reset = nullptr) {
    const float scale = ImGui::GetFontSize() / 15.5f;
    const float h = 18 * scale, r = 7 * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float y = p.y + std::max(h, ImGui::GetFrameHeight()) * 0.5f;
    ImGui::InvisibleButton(id, ImVec2(width, std::max(h, ImGui::GetFrameHeight())));
    const bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
    auto toT = [&](double x) {
        return float(logarithmic ? std::log(x / lo) / std::log(hi / lo) : (x - lo) / (hi - lo));
    };
    bool changed = false;
    if (active) {
        const float t = std::clamp((ImGui::GetIO().MousePos.x - p.x - r) / (width - 2 * r), 0.0f, 1.0f);
        const double nv = logarithmic ? lo * std::pow(hi / lo, double(t)) : lo + (hi - lo) * t;
        if (nv != *v) {
            *v = nv;
            changed = true;
        }
    }
    if (reset && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        *v = *reset;
        changed = true;
    }
    const float t = std::clamp(toT(*v), 0.0f, 1.0f);
    const float x0 = p.x + r, x1 = p.x + width - r, kx = x0 + (x1 - x0) * t;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float th = 2 * scale;
    dl->AddRectFilled(ImVec2(x0, y - th), ImVec2(x1, y + th), theme::rgba(1, 1, 1, 0.10f), th);
    dl->AddRectFilled(ImVec2(x0, y - th), ImVec2(kx, y + th), ImGui::GetColorU32(theme::accent), th);
    if (hovered || active) dl->AddCircleFilled(ImVec2(kx, y), r * 1.7f, theme::rgba(theme::accent.x, theme::accent.y, theme::accent.z, 0.18f), 24);
    dl->AddCircleFilled(ImVec2(kx, y), r, theme::rgba(0.93f, 0.96f, 0.98f), 24);
    dl->AddCircle(ImVec2(kx, y), r, ImGui::GetColorU32(theme::accent), 24, 1.5f * scale);
    return changed;
}

// Title and value on one line, full-width slider underneath (ParamSlider in Theme.swift).
bool paramSlider(const char* title, double* value, double lo, double hi, const Formatter& format,
                 const UiFonts& fonts, bool logarithmic = false) {
    ImGui::PushID(title);
    ImGui::PushFont(fonts.caption);
    ImGui::TextColored(theme::textDim, "%s", title);
    const std::string text = format(*value);
    ImGui::SameLine(regionRight() - ImGui::CalcTextSize(text.c_str()).x);
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopFont();
    const bool changed = slimSlider("##s", value, lo, hi, ImGui::GetContentRegionAvail().x, logarithmic);
    ImGui::PopID();
    return changed;
}

// Rounded section card with a small caps title: an auto-height child window, so right-aligned
// widgets inside measure against the card rather than the sidebar.
struct Section {
    Section(const char* title, const UiFonts& fonts, float scale) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1, 1, 1, 0.03f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * scale, 10 * scale));
        ImGui::BeginChild(title, ImVec2(-FLT_MIN, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(theme::textDim, "%s", title);
        ImGui::PopFont();
        width = ImGui::GetContentRegionAvail().x;
    }
    ~Section() {
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
    float width = 0;
};

void levelMeter(float level, ImVec2 size) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = size.y * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), theme::rgba(1, 1, 1, 0.08f), r);
    const float db = 20.0f * std::log10(std::max(level, 1e-5f));
    const float frac = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    if (frac <= 0) return;
    // accent -> green -> yellow -> red across the full width, clipped to the level.
    const ImU32 stops[4] = {ImGui::GetColorU32(theme::accent), theme::rgba(0.3f, 0.9f, 0.4f), theme::rgba(1, 0.85f, 0.2f),
                            theme::rgba(1, 0.25f, 0.2f)};
    dl->PushClipRect(p, ImVec2(p.x + size.x * frac, p.y + size.y), true);
    const float seg = size.x / 3;
    for (int i = 0; i < 3; ++i) {
        dl->AddRectFilledMultiColor(ImVec2(p.x + seg * i, p.y), ImVec2(p.x + seg * (i + 1), p.y + size.y), stops[i],
                                    stops[i + 1], stops[i + 1], stops[i]);
    }
    dl->PopClipRect();
}

bool powerButton(bool on, float scale) {
    const float d = 30 * scale;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton("##power", ImVec2(d, d));
    const ImVec2 c(p.x + d / 2, p.y + d / 2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hover = ImGui::IsItemHovered();
    dl->AddCircleFilled(c, d / 2, on ? ImGui::GetColorU32(theme::accent) : theme::rgba(1, 1, 1, hover ? 0.16f : 0.1f), 32);
    const ImU32 fg = on ? theme::rgba(0, 0, 0) : theme::rgba(1, 1, 1);
    const float r = d * 0.24f, th = 2.2f * scale;
    dl->PathArcTo(c, r, -3.14159f / 2 + 0.75f, -3.14159f / 2 + 2 * 3.14159f - 0.75f, 24);
    dl->PathStroke(fg, 0, th);
    dl->AddLine(ImVec2(c.x, c.y - r - 2 * scale), ImVec2(c.x, c.y - r * 0.2f), fg, th);
    if (hover) ImGui::SetTooltip(on ? "Bypass processing" : "Enable processing");
    return clicked;
}

std::wstring fileDialog(void* hwnd, bool save, const wchar_t* defaultName) {
    wchar_t path[MAX_PATH * 2] = {};
    if (defaultName) wcsncpy_s(path, defaultName, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = static_cast<HWND>(hwnd);
    ofn.lpstrFilter = L"Parametric EQ (*.txt)\0*.txt\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = DWORD(std::size(path));
    ofn.lpstrDefExt = L"txt";
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    return ok ? std::wstring(path) : std::wstring();
}

void openSoundSettings() { ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL); }

// ---------------------------------------------------------------------------------------------
// Top bar

struct UiState {
    bool openSavePreset = false;
    char presetName[64] = "";
};
UiState g;

constexpr char kAllAppsLabel[] = "All apps";

std::string deviceLabel(const App& app, const std::wstring& id) {
    if (id == kAllAppsSource) return kAllAppsLabel;
    const AudioDevice* d = const_cast<App&>(app).devices().find(id);
    return d ? d->name : std::string("(none)");
}

// `offerAllApps` adds the "All apps" capture option (source picker only).
bool deviceCombo(App& app, const char* id, const std::wstring& choice, const std::wstring& resolved,
                 const std::wstring& exclude, bool offerAllApps, std::wstring& picked) {
    const std::string preview = (choice.empty() ? "Automatic: " : "") + deviceLabel(app, resolved);
    bool changed = false;
    if (ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        const std::string autoLabel = "Automatic (" + deviceLabel(app, resolved) + ")";
        if (ImGui::Selectable(autoLabel.c_str(), choice.empty())) { picked.clear(); changed = true; }
        ImGui::Separator();
        if (offerAllApps) {
            if (ImGui::Selectable("All apps   [no cable needed]", choice == kAllAppsSource)) {
                picked = kAllAppsSource;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Captures every app directly and turns them down in the Windows volume mixer while\n"
                                  "SpatialEQ plays them, so it works with just one pair of headphones or speakers.");
        }
        for (const auto& d : app.devices().devices()) {
            std::string label = d.name;
            if (d.isDefault) label += "   [Windows default]";
            if (d.isVirtual) label += "   [virtual]";
            const bool disabled = d.id == exclude;
            if (disabled) ImGui::BeginDisabled();
            if (ImGui::Selectable(label.c_str(), d.id == choice)) { picked = d.id; changed = true; }
            if (disabled) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void presetMenu(App& app, float scale) {
    std::string label = app.presetName() + (app.presetModified() ? " (edited)" : "");
    ImGui::SetNextItemWidth(210 * scale);
    if (ImGui::BeginCombo("##preset", label.c_str(), ImGuiComboFlags_HeightLargest)) {
        ImGui::TextDisabled("Built-in");
        for (const auto& p : app.builtIns())
            if (ImGui::Selectable(p.name.c_str(), p.name == app.presetName())) app.applyPreset(p);
        if (!app.userPresets().empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("My Presets");
            const auto presets = app.userPresets(); // copy: applying may not invalidate, but deleting could
            for (const auto& p : presets)
                if (ImGui::Selectable((p.name + "##user").c_str(), p.name == app.presetName())) app.applyPreset(p);
        }
        ImGui::Separator();
        if (ImGui::Selectable("Save as New Preset...")) {
            g.openSavePreset = true;
            g.presetName[0] = 0;
        }
        if (app.presetIsUser()) {
            if (ImGui::Selectable(("Update \"" + app.presetName() + "\"").c_str())) app.updateCurrentPreset();
            if (ImGui::Selectable(("Delete \"" + app.presetName() + "\"").c_str())) app.deleteCurrentPreset();
        }
        ImGui::EndCombo();
    }
}

void topBar(App& app, const UiFonts& fonts, float scale) {
    const auto st = app.routeStatus();
    if (powerButton(app.enabled, scale)) app.enabled = !app.enabled;
    ImGui::SameLine(0, 12 * scale);

    ImGui::BeginGroup();
    ImGui::PushFont(fonts.title);
    ImGui::TextUnformatted("SpatialEQ");
    ImGui::PopFont();
    ImGui::PushFont(fonts.caption);
    switch (st.state) {
    case AudioRouter::State::Stopped: ImGui::TextColored(theme::textDim, "Stopped"); break;
    case AudioRouter::State::Starting: ImGui::TextColored(theme::textDim, "Starting..."); break;
    case AudioRouter::State::Running:
        if (app.capturingAllApps()) {
            const int n = app.appsInMix();
            ImGui::TextColored(theme::textDim, "%.0f kHz  -  %.0f ms  -  %s %d app%s", st.sampleRate / 1000.0,
                               st.latencyMs, app.enabled ? "processing" : "bypassing", n, n == 1 ? "" : "s");
        } else {
            ImGui::TextColored(theme::textDim, "%.0f kHz  -  %.0f ms  -  %s", st.sampleRate / 1000.0, st.latencyMs,
                               app.enabled ? "processing" : "bypassed");
        }
        break;
    case AudioRouter::State::Failed:
        ImGui::TextColored(theme::warning, "%s", st.message.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Retry")) app.restartRoute();
        break;
    }
    ImGui::PopFont();
    ImGui::EndGroup();

    // Right side: preset, devices, meters.
    const float metersW = 90 * scale, comboW = 230 * scale, presetW = 210 * scale;
    const float right = regionRight();
    const float startX = right - (metersW + 2 * comboW + presetW + 3 * 12 * scale + 50 * scale);
    ImGui::SameLine(std::max(startX, ImGui::GetCursorPosX() + 20 * scale));
    const float rowY = ImGui::GetCursorPosY();
    ImGui::SetCursorPosY(rowY + 4 * scale);
    presetMenu(app, scale);

    const std::wstring src = app.resolvedSource(), out = app.resolvedOutput();
    std::wstring picked;
    ImGui::SameLine(0, 12 * scale);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(comboW);
    if (deviceCombo(app, "##source", app.sourceChoice(), src, src == kAllAppsSource ? std::wstring() : out,
                    app.allAppsAvailable(), picked))
        app.setSource(picked);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Source: what SpatialEQ captures (every app, or one playback device)");
    ImGui::EndGroup();
    ImGui::SameLine(0, 6 * scale);
    ImGui::TextColored(theme::textDim, ">");
    ImGui::SameLine(0, 6 * scale);
    ImGui::SetNextItemWidth(comboW);
    if (deviceCombo(app, "##output", app.outputChoice(), out, src == kAllAppsSource ? std::wstring() : src, false,
                    picked))
        app.setOutput(picked);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Output: where you hear the processed sound");

    ImGui::SameLine(0, 12 * scale);
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, 3 * scale));
    levelMeter(app.meters().peakL, ImVec2(metersW, 5 * scale));
    levelMeter(app.meters().peakR, ImVec2(metersW, 5 * scale));
    ImGui::EndGroup();
    if (app.meters().limiterReductionDb < -0.5f) {
        ImGui::SameLine();
        ImGui::PushFont(fonts.caption);
        ImGui::TextColored(theme::warning, "LIM %.0f", app.meters().limiterReductionDb);
        ImGui::PopFont();
    }
}

// Explains the routing when it cannot work as-is (Windows has no tap that also mutes the original).
void routingNotice(App& app, const UiFonts& fonts, float scale) {
    if (app.resolvedSource() == kAllAppsSource) {
        if (app.devices().find(app.resolvedOutput())) return;
    }
    const AudioDevice* src = app.devices().find(app.resolvedSource());
    const AudioDevice* out = app.devices().find(app.resolvedOutput());
    const AudioDevice* def = app.devices().defaultDevice();
    std::string text;
    bool openSettings = false;
    if (!src && !out) {
        text = "No playback device found.";
    } else if (!src) {
        text = "Choose what to capture.";
    } else if (!out) {
        text = "Add a second playback device (headphones, speakers or a virtual cable) so SpatialEQ has somewhere to "
               "play the processed sound.";
    } else if (!src->isVirtual) {
        text = "Capturing \"" + src->name + "\" directly, so its unprocessed sound is still audible." +
               std::string(app.allAppsAvailable()
                               ? " Choose \"All apps\" as the source to process everything on one device instead."
                               : " For clean system-wide processing install a virtual audio cable (for example "
                                 "VB-CABLE), make it the Windows default playback device and pick your headphones or "
                                 "speakers as the output.");
        openSettings = !app.allAppsAvailable();
    } else if (def && def->id != src->id) {
        text = "Apps are playing to \"" + def->name + "\". Make \"" + src->name +
               "\" the Windows default playback device so their sound goes through SpatialEQ.";
        openSettings = true;
    }
    if (text.empty()) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.98f, 0.62f, 0.27f, 0.10f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8 * scale);
    ImGui::BeginChild("##notice", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(fonts.caption);
    ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x - (openSettings ? 130 * scale : 0));
    ImGui::TextColored(theme::accent2, "%s", text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::PopFont();
    if (openSettings) {
        ImGui::SameLine(regionRight() - 120 * scale);
        if (ImGui::Button("Sound settings", ImVec2(120 * scale, 0))) openSoundSettings();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------------------------
// Sidebar

auto pct = [](double v) { return fmt("%.0f%%", v * 100); };

void sidebar(App& app, const UiFonts& fonts, float scale) {
    SoundSettings& s = app.settings;

    {
        Section sec("OUTPUT MODE", fonts, scale);
        const float w = (sec.width - 4 * scale) / 2;
        for (int i = 0; i < 2; ++i) {
            if (i) ImGui::SameLine(0, 4 * scale);
            const bool on = int(s.mode) == i;
            if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::accent.x, theme::accent.y, theme::accent.z, 0.3f));
            if (ImGui::Button(i == 0 ? "Headphones" : "Speakers", ImVec2(w, 0))) s.mode = OutputMode(i);
            if (on) ImGui::PopStyleColor();
        }
        if (const AudioDevice* d = app.devices().find(app.resolvedOutput())) {
            if (d->headphoneLike != (s.mode == OutputMode::Headphones)) {
                ImGui::PushFont(fonts.caption);
                ImGui::PushTextWrapPos(0);
                ImGui::TextColored(theme::warning, "%s looks like %s.", d->name.c_str(),
                                   d->headphoneLike ? "headphones" : "speakers");
                ImGui::PopTextWrapPos();
                ImGui::PopFont();
            }
        }
    }

    if (s.mode == OutputMode::Headphones) {
        Section sec("SPATIAL AUDIO", fonts, scale);
        toggleRow("Virtual surround", &s.spatialEnabled, scale);
        if (s.spatialEnabled) {
            const float w = (sec.width - 8 * scale) / 3;
            for (int i = 0; i < 3; ++i) {
                if (i) ImGui::SameLine(0, 4 * scale);
                const bool on = int(s.upmix) == i;
                if (on) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(theme::accent.x, theme::accent.y, theme::accent.z, 0.3f));
                if (ImGui::Button(i == 0 ? "Stereo" : i == 1 ? "5.1" : "7.1", ImVec2(w, 0))) s.upmix = Upmix(i);
                if (on) ImGui::PopStyleColor();
            }
            if (ImGui::Button("Reset source positions", ImVec2(-FLT_MIN, 0))) s.resetSources();
        }
    } else {
        Section sec("STEREO IMAGE", fonts, scale);
        paramSlider("Width", &s.width, 0, 2.5, pct, fonts);
        toggleRow("Crosstalk cancellation", &s.crosstalkEnabled, scale);
        if (s.crosstalkEnabled) {
            paramSlider("Strength", &s.crosstalkStrength, 0, 1, pct, fonts);
            paramSlider("Speaker span", &s.speakerSpan, 10, 120, [](double v) { return fmt("%.0f\xC2\xB0", v); }, fonts);
            ImGui::PushFont(fonts.caption);
            ImGui::TextColored(theme::textDim, "Works best sitting centred between the speakers.");
            ImGui::PopFont();
        }
    }

    {
        Section sec("ROOM", fonts, scale);
        paramSlider("Room size", &s.roomSize, 0, 1, pct, fonts);
        paramSlider("Reverb", &s.reverbMix, 0, 1, pct, fonts);
    }
    {
        Section sec("ENHANCE", fonts, scale);
        paramSlider("Bass boost", &s.bassBoostDb, 0, 12, [](double v) { return fmt("%.1f dB", v); }, fonts);
        paramSlider("Dialogue boost", &s.dialogueBoost, 0, 1, pct, fonts);
        toggleRow("Volume leveler", &s.levelerEnabled, scale);
        if (s.levelerEnabled) {
            paramSlider("Target loudness", &s.levelerTargetDb, -30, -10, [](double v) { return fmt("%.0f dB", v); }, fonts);
            ImGui::PushFont(fonts.caption);
            ImGui::TextColored(theme::textDim, "Leveler gain %+.1f dB", app.meters().levelerGainDb);
            ImGui::PopFont();
        }
    }
    {
        Section sec("OUTPUT", fonts, scale);
        toggleRow("Limiter", &s.limiterEnabled, scale);
        if (s.limiterEnabled)
            paramSlider("Ceiling", &s.limiterCeilingDb, -6, 0, [](double v) { return fmt("%.1f dB", v); }, fonts);
        paramSlider("Output gain", &s.outputGainDb, -12, 12, [](double v) { return fmt("%+.1f dB", v); }, fonts);
    }
    {
        Section sec("APP", fonts, scale);
        bool startup = app.startWithWindows();
        if (toggleRow("Start with Windows", &startup, scale)) app.setStartWithWindows(startup);
        toggleRow("Keep running in the tray", &app.closeToTray, scale);
        if (app.allAppsAvailable()) {
            if (ImGui::Button("Reset app volumes", ImVec2(-FLT_MIN, 0))) {
                const int n = app.resetAppVolumes();
                app.lastError = n > 0 ? std::to_string(n) + " app volume(s) restored in the Windows volume mixer."
                                      : "No app volumes needed restoring.";
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("\"All apps\" turns other apps down in the Windows volume mixer while SpatialEQ\n"
                                  "plays them. This puts any that are still turned down back to their volume.");
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Parametric EQ editor

constexpr double kMinF = 20, kMaxF = 20000, kRangeDb = 18;

// Fills the area between a polyline and a horizontal baseline as one triangle strip. Unlike
// per-segment AddQuadFilled / AddRectFilledMultiColor this has no anti-aliased seams between
// neighbouring segments. `lineColor` applies at the curve, `baseColor` at the baseline.
void fillToBaseline(ImDrawList* dl, const ImVec2* pts, int n, float baseY, ImU32 lineColor, ImU32 baseColor) {
    if (n < 2) return;
    const ImVec2 uv = ImGui::GetFontTexUvWhitePixel();
    dl->PrimReserve((n - 1) * 6, n * 2);
    const auto first = ImDrawIdx(dl->_VtxCurrentIdx);
    for (int i = 0; i < n; ++i) {
        dl->PrimWriteVtx(pts[i], uv, lineColor);
        dl->PrimWriteVtx(ImVec2(pts[i].x, baseY), uv, baseColor);
    }
    for (int i = 0; i < n - 1; ++i) {
        const auto a = ImDrawIdx(first + i * 2);
        dl->PrimWriteIdx(a);
        dl->PrimWriteIdx(ImDrawIdx(a + 1));
        dl->PrimWriteIdx(ImDrawIdx(a + 2));
        dl->PrimWriteIdx(ImDrawIdx(a + 1));
        dl->PrimWriteIdx(ImDrawIdx(a + 3));
        dl->PrimWriteIdx(ImDrawIdx(a + 2));
    }
}

struct EqDrag {
    int band = -1;
    bool moved = false;
};
EqDrag eqDrag;

void eqHeader(App& app, const UiFonts& fonts, float scale, void* hwnd) {
    SoundSettings& s = app.settings;
    ImGui::PushFont(fonts.caption);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textDim, "PARAMETRIC EQ");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3 * scale);
    toggle("##eqon", &s.eqEnabled, scale);

    const float right = regionRight();
    ImGui::SameLine(right - 520 * scale);
    ImGui::PushFont(fonts.caption);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(theme::textDim, "Preamp");
    ImGui::PopFont();
    ImGui::SameLine();
    static const double kZero = 0;
    slimSlider("##preamp", &s.preampDb, -24, 6, 130 * scale, false, &kZero);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%+5.1f dB", s.preampDb);
    ImGui::SameLine(0, 12 * scale);
    const bool canAdd = int(s.bands.size()) < SoundSettings::kMaxBands;
    if (!canAdd) ImGui::BeginDisabled();
    if (ImGui::Button("+", ImVec2(28 * scale, 0))) {
        EqBand b;
        b.frequency = 1000;
        s.bands.push_back(b);
        app.selectedBand = int(s.bands.size()) - 1;
    }
    if (!canAdd) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Add band");
    ImGui::SameLine();
    const bool canRemove = app.selectedBand >= 0 && app.selectedBand < int(s.bands.size());
    if (!canRemove) ImGui::BeginDisabled();
    if (ImGui::Button("-", ImVec2(28 * scale, 0))) {
        s.bands.erase(s.bands.begin() + app.selectedBand);
        app.selectedBand = -1;
    }
    if (!canRemove) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Remove selected band");
    ImGui::SameLine();
    if (ImGui::Button("AutoEq...")) ImGui::OpenPopup("eqmenu");
    if (ImGui::BeginPopup("eqmenu")) {
        if (ImGui::MenuItem("Import AutoEq / Equalizer APO file...")) {
            const std::wstring path = fileDialog(hwnd, false, nullptr);
            if (!path.empty()) {
                const std::string err = app.importAutoEq(path);
                if (!err.empty()) app.lastError = err;
            }
        }
        if (ImGui::MenuItem("Export as ParametricEQ.txt...")) {
            const std::wstring path = fileDialog(hwnd, true, L"ParametricEQ.txt");
            if (!path.empty()) {
                const std::string err = app.exportAutoEq(path);
                if (!err.empty()) app.lastError = err;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset to 10-band")) app.resetEq();
        ImGui::EndPopup();
    }
}

void eqGraph(App& app, const UiFonts& fonts, ImVec2 size, float scale) {
    SoundSettings& s = app.settings;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1(p0.x + size.x, p0.y + size.y);
    ImGui::InvisibleButton("##eqgraph", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(p0, p1, true);

    const double logSpan = std::log(kMaxF / kMinF);
    auto xOf = [&](double f) { return float(p0.x + size.x * std::log(f / kMinF) / logSpan); };
    auto fOf = [&](float x) { return kMinF * std::exp(double(x - p0.x) / size.x * logSpan); };
    const float midY = p0.y + size.y * 0.5f;
    auto yOf = [&](double db) { return float(midY - db / kRangeDb * size.y * 0.5); };
    auto dbOf = [&](float y) { return (midY - y) / (size.y * 0.5) * kRangeDb; };

    // Grid
    ImGui::PushFont(fonts.caption);
    const ImU32 grid = theme::rgba(1, 1, 1, 0.06f), gridText = theme::rgba(1, 1, 1, 0.35f);
    for (double f : {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0}) {
        const float x = xOf(f);
        dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), grid);
        dl->AddText(ImVec2(x + 3 * scale, p1.y - ImGui::GetFontSize() - 2 * scale), gridText, formatHz(f).c_str());
    }
    for (int db = -12; db <= 12; db += 6) {
        const float y = yOf(db);
        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), db == 0 ? theme::rgba(1, 1, 1, 0.14f) : grid);
        char t[8];
        std::snprintf(t, sizeof(t), "%+d", db);
        dl->AddText(ImVec2(p0.x + 4 * scale, y - ImGui::GetFontSize() - 1), gridText, db == 0 ? "0" : t);
    }
    ImGui::PopFont();

    // Live spectrum (post-processing), as a soft filled area.
    {
        const float* bands = app.spectrum();
        const ImU32 top = theme::rgba(theme::accent.x, theme::accent.y, theme::accent.z, 0.28f);
        const ImU32 bottom = theme::rgba(theme::accent.x, theme::accent.y, theme::accent.z, 0.02f);
        ImVec2 pts[App::kSpectrumBands];
        for (int i = 0; i < App::kSpectrumBands; ++i)
            pts[i] = ImVec2(xOf(app.spectrumFrequency(i)), p1.y - bands[i] * size.y * 0.92f);
        fillToBaseline(dl, pts, App::kSpectrumBands, p1.y, top, bottom);
        dl->AddPolyline(pts, App::kSpectrumBands, theme::rgba(theme::accent.x, theme::accent.y, theme::accent.z, 0.45f), 0,
                        1.2f * scale);
    }

    // Combined response.
    {
        constexpr int kPoints = 240;
        float freqs[kPoints], db[kPoints];
        for (int i = 0; i < kPoints; ++i) freqs[i] = float(kMinF * std::exp(logSpan * i / (kPoints - 1)));
        const sq_params params = s.toParams(true);
        sq_eq_response(&params, app.sampleRate(), freqs, db, kPoints);
        ImVec2 pts[kPoints];
        for (int i = 0; i < kPoints; ++i) pts[i] = ImVec2(xOf(freqs[i]), yOf(std::clamp<double>(db[i], -40, 40)));
        const ImU32 c = s.eqEnabled ? theme::rgba(1, 1, 1, 0.92f) : theme::rgba(1, 1, 1, 0.3f);
        // Fill between the curve and 0 dB.
        const ImU32 fill = theme::rgba(theme::accent2.x, theme::accent2.y, theme::accent2.z, s.eqEnabled ? 0.16f : 0.04f);
        fillToBaseline(dl, pts, kPoints, midY, fill, theme::rgba(theme::accent2.x, theme::accent2.y, theme::accent2.z, 0.0f));
        dl->AddPolyline(pts, kPoints, c, 0, 2.0f * scale);
    }

    // Band handles.
    const float r = 8 * scale;
    auto handlePos = [&](const EqBand& b) {
        return ImVec2(xOf(std::clamp(b.frequency, kMinF, kMaxF)), yOf(hasGain(b.type) ? b.gainDb : 0));
    };
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    int hoverBand = -1;
    float best = r * 1.6f;
    for (int i = 0; i < int(s.bands.size()); ++i) {
        const ImVec2 hp = handlePos(s.bands[i]);
        const float d = std::hypot(mouse.x - hp.x, mouse.y - hp.y);
        if (hovered && d < best) { best = d; hoverBand = i; }
    }

    // Interaction
    ImGuiIO& io = ImGui::GetIO();
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        eqDrag = {hoverBand, false};
        app.selectedBand = hoverBand;
    }
    if (eqDrag.band >= 0 && eqDrag.band < int(s.bands.size()) && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::IsItemActive()) {
        if (io.MouseDelta.x != 0 || io.MouseDelta.y != 0) eqDrag.moved = true;
        if (eqDrag.moved) {
            EqBand& b = s.bands[eqDrag.band];
            b.frequency = std::clamp(fOf(std::clamp(mouse.x, p0.x, p1.x)), kMinF, kMaxF);
            if (hasGain(b.type)) b.gainDb = std::round(std::clamp(dbOf(mouse.y), -kRangeDb, kRangeDb) * 10) / 10;
        }
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) eqDrag.band = -1;
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (hoverBand >= 0) {
            s.bands[hoverBand].enabled = !s.bands[hoverBand].enabled;
        } else if (int(s.bands.size()) < SoundSettings::kMaxBands) {
            EqBand b;
            b.frequency = std::clamp(fOf(mouse.x), kMinF, kMaxF);
            b.gainDb = std::round(std::clamp(dbOf(mouse.y), -kRangeDb, kRangeDb) * 10) / 10;
            s.bands.push_back(b);
            app.selectedBand = int(s.bands.size()) - 1;
        }
    }
    const int wheelBand = eqDrag.band >= 0 ? eqDrag.band : hoverBand;
    if (hovered && wheelBand >= 0 && io.MouseWheel != 0) {
        EqBand& b = s.bands[wheelBand];
        b.q = std::clamp(b.q * std::pow(1.15, io.MouseWheel), 0.1, 20.0);
    }
    if (hovered && hoverBand >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        app.selectedBand = hoverBand;
        ImGui::OpenPopup("bandmenu");
    }

    for (int i = 0; i < int(s.bands.size()); ++i) {
        const EqBand& b = s.bands[i];
        const ImVec2 hp = handlePos(b);
        const bool sel = i == app.selectedBand;
        const ImU32 c = b.enabled ? theme::channelColor(i % 8) : theme::rgba(0.5f, 0.5f, 0.5f);
        if (sel) dl->AddCircleFilled(hp, r * 1.9f, (c & 0x00FFFFFF) | 0x30000000, 24);
        dl->AddCircleFilled(hp, r, (c & 0x00FFFFFF) | (b.enabled ? 0xE0000000 : 0x70000000), 20);
        dl->AddCircle(hp, r, sel || i == hoverBand ? theme::rgba(1, 1, 1) : theme::rgba(1, 1, 1, 0.4f), 20, 1.5f * scale);
        char n[4];
        std::snprintf(n, sizeof(n), "%d", i + 1);
        ImGui::PushFont(fonts.caption);
        const ImVec2 ts = ImGui::CalcTextSize(n);
        dl->AddText(ImVec2(hp.x - ts.x / 2, hp.y - ts.y / 2), theme::rgba(0.02f, 0.03f, 0.05f), n);
        ImGui::PopFont();
    }
    dl->PopClipRect();

    const int tipBand = eqDrag.band >= 0 ? eqDrag.band : hoverBand;
    if (tipBand >= 0 && tipBand < int(s.bands.size())) {
        const EqBand& b = s.bands[tipBand];
        if (hasGain(b.type))
            ImGui::SetTooltip("%d  %s  %s Hz  %+.1f dB  Q %.2f\nDrag to move, scroll for Q, double-click to toggle",
                              tipBand + 1, label(b.type), formatHz(b.frequency).c_str(), b.gainDb, b.q);
        else
            ImGui::SetTooltip("%d  %s  %s Hz  Q %.2f\nDrag to move, scroll for Q, double-click to toggle", tipBand + 1,
                              label(b.type), formatHz(b.frequency).c_str(), b.q);
    } else if (hovered && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        ImGui::SetTooltip("%s Hz  %+.1f dB\nDouble-click to add a band", formatHz(fOf(mouse.x)).c_str(), dbOf(mouse.y));
    }

    if (ImGui::BeginPopup("bandmenu")) {
        const int i = app.selectedBand;
        if (i >= 0 && i < int(s.bands.size())) {
            EqBand& b = s.bands[i];
            ImGui::MenuItem("Enabled", nullptr, &b.enabled);
            ImGui::Separator();
            for (int t = 0; t < 5; ++t)
                if (ImGui::MenuItem(label(BandType(t)), nullptr, int(b.type) == t)) b.type = BandType(t);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete band")) {
                s.bands.erase(s.bands.begin() + i);
                app.selectedBand = -1;
            }
        }
        ImGui::EndPopup();
    }
}

void bandInspector(App& app, const UiFonts& fonts, float scale) {
    SoundSettings& s = app.settings;
    const int i = app.selectedBand;
    if (i < 0 || i >= int(s.bands.size())) return;
    EqBand& b = s.bands[i];
    ImGui::PushID(i);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(theme::channelColor(i % 8)), "Band %d", i + 1);
    ImGui::SameLine();
    toggle("##on", &b.enabled, scale);
    ImGui::SameLine(0, 14 * scale);
    ImGui::SetNextItemWidth(120 * scale);
    if (ImGui::BeginCombo("##type", label(b.type))) {
        for (int t = 0; t < 5; ++t)
            if (ImGui::Selectable(label(BandType(t)), int(b.type) == t)) b.type = BandType(t);
        ImGui::EndCombo();
    }
    const float avail = ImGui::GetContentRegionAvail().x - 90 * scale;
    const float w = avail / 3 - 120 * scale;
    auto field = [&](const char* name, const char* id, double* v, double lo, double hi, const char* format, bool log) {
        ImGui::SameLine(0, 14 * scale);
        ImGui::PushFont(fonts.caption);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(theme::textDim, "%s", name);
        ImGui::PopFont();
        ImGui::SameLine();
        slimSlider(id, v, lo, hi, w, log);
        ImGui::SameLine();
        char text[32];
        std::snprintf(text, sizeof(text), format, *v);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
    };
    field("Freq", "##f", &b.frequency, kMinF, kMaxF, "%.0f Hz", true);
    if (hasGain(b.type)) {
        field("Gain", "##g", &b.gainDb, -kRangeDb, kRangeDb, "%+.1f dB", false);
    } else {
        ImGui::SameLine(0, 14 * scale);
        ImGui::Dummy(ImVec2(w + ImGui::CalcTextSize("Gain").x + ImGui::CalcTextSize("+00.0 dB").x + 16 * scale, 0));
    }
    field("Q", "##q", &b.q, 0.1, 20, "%.2f", true);
    ImGui::SameLine(regionRight() - 70 * scale);
    if (ImGui::Button("Delete", ImVec2(70 * scale, 0))) {
        s.bands.erase(s.bands.begin() + i);
        app.selectedBand = -1;
    }
    ImGui::PopID();
}

void modals(App& app, float scale) {
    if (g.openSavePreset) {
        ImGui::OpenPopup("Save Preset");
        g.openSavePreset = false;
    }
    ImGui::SetNextWindowSize(ImVec2(320 * scale, 0));
    if (ImGui::BeginPopupModal("Save Preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-FLT_MIN);
        const bool enter = ImGui::InputTextWithHint("##name", "Name", g.presetName, sizeof(g.presetName),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const float bw = 90 * scale;
        ImGui::SetCursorPosX(regionRight() - 2 * bw - 8 * scale);
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Save", ImVec2(bw, 0)) || enter) {
            app.saveAsPreset(g.presetName);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!app.lastError.empty() && !ImGui::IsPopupOpen("SpatialEQ##error")) ImGui::OpenPopup("SpatialEQ##error");
    ImGui::SetNextWindowSize(ImVec2(360 * scale, 0));
    if (ImGui::BeginPopupModal("SpatialEQ##error", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::PushTextWrapPos(0);
        ImGui::TextUnformatted(app.lastError.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::SetCursorPosX(regionRight() - 90 * scale);
        if (ImGui::Button("OK", ImVec2(90 * scale, 0)) || ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            app.lastError.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace

void drawUi(App& app, const UiFonts& fonts, float scale, void* hwnd) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("SpatialEQ", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);

    topBar(app, fonts, scale);
    routingNotice(app, fonts, scale);
    ImGui::Spacing();

    const float gap = 10 * scale;
    const bool inspector = app.selectedBand >= 0 && app.selectedBand < int(app.settings.bands.size());
    const float eqH = (inspector ? 290 : 250) * scale;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float upperH = std::max(200 * scale, avail.y - eqH - gap);
    const float sideW = 320 * scale;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##scene", ImVec2(avail.x - sideW - gap, upperH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawScene(app, fonts, ImGui::GetContentRegionAvail(), scale);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("##sidebar", ImVec2(sideW, upperH), ImGuiChildFlags_Borders);
    sidebar(app, fonts, scale);
    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
    ImGui::BeginChild("##eq", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    eqHeader(app, fonts, scale, hwnd);
    const float inspH = inspector ? ImGui::GetFrameHeightWithSpacing() + 6 * scale : 0;
    const ImVec2 graph(ImGui::GetContentRegionAvail().x, std::max(80 * scale, ImGui::GetContentRegionAvail().y - inspH));
    eqGraph(app, fonts, graph, scale);
    if (inspector) {
        ImGui::Dummy(ImVec2(0, 2 * scale));
        bandInspector(app, fonts, scale);
    }
    ImGui::EndChild();

    modals(app, scale);
    ImGui::End();
}

} // namespace sq::app
