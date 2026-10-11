#include "App.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>

namespace sq::app {

namespace {

using Clock = std::chrono::steady_clock;

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"SpatialEQ";

std::wstring settingsPath() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) dir = appData;
    CoTaskMemFree(appData);
    if (dir.empty()) return L"SpatialEQ-settings.txt";
    dir += L"\\SpatialEQ";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.txt";
}

std::wstring exePath() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    return std::wstring(buf, n);
}

bool readFile(const std::wstring& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool writeFileAtomically(const std::wstring& path, const std::string& data) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << data;
        if (!f) return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

} // namespace

App::App() : engine_(sq_engine_create()), router_(engine_) {
    spectrum_ = sq_spectrum_create(kSpectrumBands);
    builtIns_ = builtInPresets();
    presetBaseline_ = builtIns_.front().settings;
}

App::~App() {
    shutdown();
    sq_spectrum_destroy(spectrum_);
    sq_engine_destroy(engine_);
}

void App::start(void* hwnd) {
    devices_.setNotifyWindow(hwnd);
    load();
    savedSnapshot_ = snapshot();

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        startWithWindows_ = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
    }

    pushParams();
    started_ = true;
    restartRoute();
}

void App::shutdown() {
    if (!started_) return;
    started_ = false;
    router_.stop();
    ducker_.stop(); // give every app its volume back
    save();
}

void App::tick() {
    const auto now = Clock::now();

    // Devices plugged in or out, or the Windows default changed.
    if (devices_.consumeChanged()) {
        devices_.refresh();
        failures_ = 0;
        if (ducker_.active()) ducker_.start(); // pick up sessions on new devices
        if (resolvedSource() != runningSource_ || resolvedOutput() != runningOutput_ ||
            routeStatus().state == AudioRouter::State::Failed)
            restartRoute();
    }

    // A running route died (device removed, exclusive mode, format change): rebuild with backoff.
    if (router_.consumeNeedsRestart()) {
        ++failures_;
        retryAt_ = now + std::chrono::milliseconds(failures_ <= 1 ? 300 : 2000);
    }
    if (retryAt_ != Clock::time_point{} && now >= retryAt_) {
        retryAt_ = {};
        devices_.refresh();
        if (failures_ <= 5) restartRoute();
    }

    pushParams();

    const auto st = router_.status();
    if (st.state == AudioRouter::State::Running) {
        sampleRate_ = st.sampleRate;
        failures_ = 0;
    }

    // All-apps mode: other apps are turned down only while SpatialEQ is actually playing them.
    const bool wantDuck = st.state == AudioRouter::State::Running && capturingAllApps();
    if (wantDuck && !ducker_.active()) {
        ducker_.start();
        lastDuckPoll_ = now;
    } else if (!wantDuck && ducker_.active()) {
        ducker_.stop();
    } else if (now - lastDuckPoll_ > std::chrono::milliseconds(wantDuck ? 100 : 2000)) {
        lastDuckPoll_ = now;
        ducker_.poll(); // new apps, volume changes, and recovery after a crash
    }
    if (now - lastSpectrum_ > std::chrono::milliseconds(30)) {
        lastSpectrum_ = now;
        level_ = sq_spectrum_update(spectrum_, engine_, sampleRate_, spectrumBands_);
    }
    sq_engine_get_meters(engine_, &meters_);
    if (st.state != AudioRouter::State::Running) {
        meters_.peakL = meters_.peakR = 0;
        meters_.limiterReductionDb = 0;
    }

    // Autosave a second after the last change.
    const std::string snap = snapshot();
    if (snap != savedSnapshot_) {
        if (!dirty_) {
            dirty_ = true;
            dirtySince_ = now;
        } else if (now - dirtySince_ > std::chrono::seconds(1)) {
            save();
        }
    }
}

void App::pushParams() {
    const sq_params p = settings.toParams(enabled);
    if (havePushed_ && std::memcmp(&p, &lastPushed_, sizeof(p)) == 0) return;
    sq_engine_set_params(engine_, &p);
    lastPushed_ = p;
    havePushed_ = true;
}

// -------------------------------------------------------------------------------------------------
// Presets

bool App::presetIsUser() const {
    return std::any_of(userPresets_.begin(), userPresets_.end(), [&](const Preset& p) { return p.name == presetName_; });
}

void App::applyPreset(const Preset& p) {
    settings = p.settings;
    presetName_ = p.name;
    presetBaseline_ = p.settings;
    selectedBand = -1;
}

void App::saveAsPreset(const std::string& rawName) {
    std::string name = trim(rawName);
    if (name.empty()) name = "My Preset";
    // Keep names unique, including against the built-ins.
    auto taken = [&](const std::string& n) {
        auto same = [&](const Preset& p) { return p.name == n; };
        return std::any_of(userPresets_.begin(), userPresets_.end(), same) ||
               std::any_of(builtIns_.begin(), builtIns_.end(), same);
    };
    std::string unique = name;
    for (int i = 2; taken(unique); ++i) unique = name + " " + std::to_string(i);
    userPresets_.push_back({unique, settings, false});
    presetName_ = unique;
    presetBaseline_ = settings;
}

void App::updateCurrentPreset() {
    for (auto& p : userPresets_) {
        if (p.name == presetName_) {
            p.settings = settings;
            presetBaseline_ = settings;
        }
    }
}

void App::deleteCurrentPreset() {
    userPresets_.erase(std::remove_if(userPresets_.begin(), userPresets_.end(),
                                      [&](const Preset& p) { return p.name == presetName_; }),
                       userPresets_.end());
    presetName_ = "Custom";
    presetBaseline_ = settings;
}

void App::resetEq() {
    settings.bands = SoundSettings::tenBand();
    settings.preampDb = 0;
    selectedBand = -1;
}

std::string App::importAutoEq(const std::wstring& path) {
    std::string text;
    if (!readFile(path, text)) return "Could not read the file.";
    AutoEqResult r;
    if (!parseAutoEq(text, r)) return "No parametric filters were found in this file.";
    settings.bands = r.bands;
    settings.preampDb = r.preampDb;
    settings.eqEnabled = true;
    selectedBand = -1;
    std::wstring file = path.substr(path.find_last_of(L"\\/") + 1);
    const auto dot = file.find_last_of(L'.');
    if (dot != std::wstring::npos) file.resize(dot);
    presetName_ = toUtf8(file);
    presetBaseline_ = settings;
    if (r.skipped > 0) return std::to_string(r.skipped) + " filter line(s) were skipped (unsupported or over 16 bands).";
    return {};
}

std::string App::exportAutoEq(const std::wstring& path) const {
    return writeFileAtomically(path, sq::app::exportAutoEq(settings)) ? std::string() : "Could not write the file.";
}

// -------------------------------------------------------------------------------------------------
// Routing

void App::setSource(const std::wstring& id) {
    sourceChoice_ = id;
    failures_ = 0;
    restartRoute();
}

void App::setOutput(const std::wstring& id) {
    outputChoice_ = id;
    failures_ = 0;
    restartRoute();
}

std::wstring App::resolvedSource() const {
    if (sourceChoice_ == kAllAppsSource && allAppsAvailable()) return sourceChoice_;
    if (!sourceChoice_.empty() && devices_.find(sourceChoice_)) return sourceChoice_;
    // Automatic: a virtual cable if there is one (the default one first), else every app (works with
    // a single device), else the default device.
    const AudioDevice* best = nullptr;
    for (const auto& d : devices_.devices()) {
        if (d.isVirtual && (!best || (d.isDefault && !best->isDefault))) best = &d;
    }
    if (best) return best->id;
    if (allAppsAvailable()) return kAllAppsSource;
    best = devices_.defaultDevice();
    return best ? best->id : std::wstring();
}

std::wstring App::resolvedOutput() const {
    const std::wstring src = resolvedSource();
    if (src == kAllAppsSource) {
        // Apps are captured directly, so any device works, including the one they play on.
        if (!outputChoice_.empty() && devices_.find(outputChoice_)) return outputChoice_;
        if (const AudioDevice* d = devices_.defaultDevice()) return d->id;
        return devices_.devices().empty() ? std::wstring() : devices_.devices().front().id;
    }
    if (!outputChoice_.empty() && outputChoice_ != src && devices_.find(outputChoice_)) return outputChoice_;
    // Automatic: the default device unless it is the source, else the first real (non-virtual) device,
    // headphones first.
    if (const AudioDevice* d = devices_.defaultDevice(); d && d->id != src && !d->isVirtual) return d->id;
    const AudioDevice* best = nullptr;
    for (const auto& d : devices_.devices()) {
        if (d.id == src || d.isVirtual) continue;
        if (!best || (d.headphoneLike && !best->headphoneLike)) best = &d;
    }
    return best ? best->id : std::wstring();
}

void App::restartRoute() {
    if (!started_) return;
    runningSource_ = resolvedSource();
    runningOutput_ = resolvedOutput();
    router_.start(runningSource_, runningOutput_);
}

void App::setStartWithWindows(bool on) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) !=
        ERROR_SUCCESS) {
        lastError = "Could not change the startup setting.";
        return;
    }
    LSTATUS rc;
    if (on) {
        const std::wstring cmd = L"\"" + exePath() + L"\" --minimized";
        rc = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                            DWORD((cmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, kRunValue);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    if (rc == ERROR_SUCCESS)
        startWithWindows_ = on;
    else
        lastError = "Could not change the startup setting.";
}

// -------------------------------------------------------------------------------------------------
// Persistence: %APPDATA%\SpatialEQ\settings.txt
//
//   [app]       enabled, source, output, preset, closeToTray
//   [current]   the live settings
//   [preset]    name=..., then that preset's settings (repeats per user preset)

std::string App::snapshot() const {
    std::string out = "[app]\n";
    out += "enabled=" + std::string(enabled ? "1" : "0") + "\n";
    out += "source=" + toUtf8(sourceChoice_) + "\n";
    out += "output=" + toUtf8(outputChoice_) + "\n";
    out += "preset=" + presetName_ + "\n";
    out += "presetModified=" + std::string(presetModified() ? "1" : "0") + "\n";
    out += "closeToTray=" + std::string(closeToTray ? "1" : "0") + "\n";
    out += "[current]\n";
    writeSettings(settings, out);
    for (const auto& p : userPresets_) {
        out += "[preset]\nname=" + p.name + "\n";
        writeSettings(p.settings, out);
    }
    return out;
}

void App::save() {
    const std::string snap = snapshot();
    if (writeFileAtomically(settingsPath(), snap)) savedSnapshot_ = snap;
    dirty_ = false;
}

void App::load() {
    std::string text;
    if (!readFile(settingsPath(), text)) return;

    enum class Section { None, App, Current, Preset } section = Section::None;
    SoundSettings current;
    bool haveCurrent = false;
    bool presetModifiedFlag = false;
    std::string preset;
    std::vector<Preset> presets;

    auto finishPreset = [&] {
        if (section == Section::Preset && !presets.empty()) finishRead(presets.back().settings);
        if (section == Section::Current) finishRead(current);
    };

    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[') {
            finishPreset();
            if (line == "[app]") section = Section::App;
            else if (line == "[current]") { section = Section::Current; haveCurrent = true; beginRead(current); }
            else if (line == "[preset]") { section = Section::Preset; presets.push_back({}); beginRead(presets.back().settings); }
            else section = Section::None;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        switch (section) {
        case Section::App:
            if (key == "enabled") enabled = value != "0";
            else if (key == "source") sourceChoice_ = fromUtf8(value);
            else if (key == "output") outputChoice_ = fromUtf8(value);
            else if (key == "preset") preset = value;
            else if (key == "presetModified") presetModifiedFlag = value == "1";
            else if (key == "closeToTray") closeToTray = value != "0";
            break;
        case Section::Current: readSettingsLine(current, key, value); break;
        case Section::Preset:
            if (key == "name") presets.back().name = value;
            else readSettingsLine(presets.back().settings, key, value);
            break;
        case Section::None: break;
        }
    }
    finishPreset();

    for (auto& p : presets)
        if (!p.name.empty()) userPresets_.push_back(std::move(p));
    if (haveCurrent) settings = current;
    if (!preset.empty()) {
        presetName_ = preset;
        presetBaseline_ = settings;
        // If the saved state was an edited preset, compare against the stored preset so "(edited)" survives.
        if (presetModifiedFlag) {
            for (const auto* list : {&builtIns_, &userPresets_})
                for (const auto& p : *list)
                    if (p.name == preset) presetBaseline_ = p.settings;
        }
    }
}

} // namespace sq::app
