#include "Ducker.h"

#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <shlobj.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace sq::app {

namespace {

// Marks volume changes we make, so they can be told apart from the user's.
// {6A1E1F3C-5B57-4D8E-9C1B-6E0F3A2D7B41}
const GUID kDuckContext = {0x6a1e1f3c, 0x5b57, 0x4d8e, {0x9c, 0x1b, 0x6e, 0x0f, 0x3a, 0x2d, 0x7b, 0x41}};

std::wstring journalPath() {
    PWSTR appData = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) dir = appData;
    CoTaskMemFree(appData);
    if (dir.empty()) return L"SpatialEQ-ducked.txt";
    dir += L"\\SpatialEQ";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\ducked.txt";
}

std::wstring takeString(LPWSTR s) {
    std::wstring out = s ? s : L"";
    CoTaskMemFree(s);
    return out;
}

} // namespace

SessionDucker::SessionDucker() : selfPid_(GetCurrentProcessId()) {
    loadJournal();
}

SessionDucker::~SessionDucker() {
    stop();
    for (auto* m : managers_) m->Release();
}

void SessionDucker::acquireManagers() {
    for (auto* m : managers_) m->Release();
    managers_.clear();
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                reinterpret_cast<void**>(&en))))
        return;
    IMMDeviceCollection* coll = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll))) {
        UINT n = 0;
        coll->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* dev = nullptr;
            if (FAILED(coll->Item(i, &dev))) continue;
            IAudioSessionManager2* sm = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&sm))))
                managers_.push_back(sm);
            dev->Release();
        }
        coll->Release();
    }
    en->Release();
}

void SessionDucker::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    acquireManagers(); // every active playback device, re-read on each start (devices may have changed)
    active_ = true;
    scan(true);
}

void SessionDucker::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, e] : entries_) restore(e, true);
    entries_.clear();
    active_ = false;
    saveJournal();
}

void SessionDucker::poll() {
    std::lock_guard<std::mutex> lock(mutex_);
    // While inactive, only look for journaled sessions left ducked by a previous crash.
    if (!active_ && journal_.empty()) return;
    if (managers_.empty()) acquireManagers();
    scan(active_);
}

int SessionDucker::duckedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return int(entries_.size());
}

void SessionDucker::restore(Entry& e, bool persisted) {
    if (auto* v = static_cast<ISimpleAudioVolume*>(e.volume)) {
        v->SetMasterVolume(e.original, &kDuckContext);
        v->Release();
    }
    if (auto* c = static_cast<IAudioSessionControl*>(e.control)) c->Release();
    e.volume = e.control = nullptr;
    if (persisted) journal_.erase(e.appId);
}

void SessionDucker::scan(bool duck) {
    for (auto& [id, e] : entries_) e.seen = false;
    bool journalChanged = false;

    for (IAudioSessionManager2* sm : managers_) {
        IAudioSessionEnumerator* se = nullptr;
        if (FAILED(sm->GetSessionEnumerator(&se))) continue;
        int count = 0;
        se->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* c = nullptr;
            if (FAILED(se->GetSession(i, &c))) continue;
            IAudioSessionControl2* c2 = nullptr;
            if (FAILED(c->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&c2)))) {
                c->Release();
                continue;
            }
            DWORD pid = 0;
            c2->GetProcessId(&pid);
            AudioSessionState state = AudioSessionStateInactive;
            c->GetState(&state);
            LPWSTR raw = nullptr;
            c2->GetSessionInstanceIdentifier(&raw);
            const std::wstring instance = takeString(raw);
            raw = nullptr;
            c2->GetSessionIdentifier(&raw);
            const std::wstring appId = takeString(raw);
            c2->Release();

            ISimpleAudioVolume* v = nullptr;
            if (pid == selfPid_ || instance.empty() ||
                FAILED(c->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&v)))) {
                c->Release();
                continue;
            }
            float vol = 1.0f;
            v->GetMasterVolume(&vol);
            const bool playing = state == AudioSessionStateActive;

            auto it = entries_.find(instance);
            if (it != entries_.end()) {
                Entry& e = it->second;
                e.seen = true;
                if (!duck || !playing) {
                    // Paused, closing or we are stopping: give the volume back while the app is silent.
                    // Windows remembers per-app volume, and a change made after the app has exited does
                    // not stick, so this must happen before the app quits; apps stop their audio first.
                    // An expired session keeps its journal entry, so the next launch is still fixed.
                    restore(e, /*persisted=*/state != AudioSessionStateExpired);
                    entries_.erase(it);
                    journalChanged = true;
                } else if (!looksDucked(vol, e.original)) {
                    // The user (or the app) changed its volume: take that as the new level and re-duck.
                    e.original = vol;
                    journal_[e.appId] = vol;
                    journalChanged = true;
                    v->SetMasterVolume(vol * kDuckLevel, &kDuckContext);
                }
                v->Release();
                c->Release();
                continue;
            }

            // A session we are not ducking. If the journal says it was left ducked (the app exited or
            // SpatialEQ stopped abruptly while it played), its real level is in the journal.
            float original = vol;
            auto j = journal_.find(appId);
            const bool leftDucked = j != journal_.end() && looksDucked(vol, j->second);
            if (leftDucked) original = j->second;

            if (duck && playing) {
                Entry e;
                e.identifier = instance;
                e.appId = appId;
                e.original = original;
                e.volume = v;
                e.control = c;
                e.seen = true;
                v->SetMasterVolume(original * kDuckLevel, &kDuckContext);
                journal_[appId] = original;
                journalChanged = true;
                entries_.emplace(instance, e);
                continue; // the entry keeps the references
            }
            if (j != journal_.end() && state != AudioSessionStateExpired) {
                // Put back a level left behind by an earlier run; either way the app is fine now.
                if (leftDucked) v->SetMasterVolume(original, &kDuckContext);
                journal_.erase(j);
                journalChanged = true;
            }
            v->Release();
            c->Release();
        }
        se->Release();
    }

    // Sessions that vanished without a state change (device removed): drop them, keep the journal.
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!it->second.seen) {
            restore(it->second, /*persisted=*/false);
            it = entries_.erase(it);
            journalChanged = true;
        } else {
            ++it;
        }
    }
    if (journalChanged) saveJournal();
}

int SessionDucker::resetAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [id, e] : entries_) restore(e, true);
    entries_.clear();
    if (managers_.empty()) acquireManagers();
    int fixed = 0;
    for (IAudioSessionManager2* sm : managers_) {
        IAudioSessionEnumerator* se = nullptr;
        if (FAILED(sm->GetSessionEnumerator(&se))) continue;
        int count = 0;
        se->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* c = nullptr;
            if (FAILED(se->GetSession(i, &c))) continue;
            IAudioSessionControl2* c2 = nullptr;
            ISimpleAudioVolume* v = nullptr;
            if (SUCCEEDED(c->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast<void**>(&c2))) &&
                SUCCEEDED(c->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast<void**>(&v)))) {
                LPWSTR raw = nullptr;
                c2->GetSessionIdentifier(&raw);
                const std::wstring appId = takeString(raw);
                float vol = 1.0f;
                v->GetMasterVolume(&vol);
                auto j = journal_.find(appId);
                const float original = j != journal_.end() ? j->second : 1.0f;
                // Anything at or below the duck level is ours (nobody listens at -80 dB on purpose).
                if (vol > 0 && vol <= kDuckLevel * 1.5f) {
                    v->SetMasterVolume(original, &kDuckContext);
                    ++fixed;
                }
                if (j != journal_.end()) journal_.erase(j);
            }
            if (v) v->Release();
            if (c2) c2->Release();
            c->Release();
        }
        se->Release();
    }
    saveJournal();
    // Stay active if we were: the next poll ducks whatever is playing again.
    return fixed;
}

// Journal format: one "volume<TAB>session identifier" per line.
void SessionDucker::loadJournal() {
    std::wifstream f(journalPath());
    std::wstring line;
    while (std::getline(f, line)) {
        const auto tab = line.find(L'\t');
        if (tab == std::wstring::npos) continue;
        const float v = std::wcstof(line.substr(0, tab).c_str(), nullptr);
        if (v > 0 && v <= 1.0f) journal_[line.substr(tab + 1)] = v;
    }
}

void SessionDucker::saveJournal() {
    const std::wstring path = journalPath();
    if (journal_.empty()) {
        DeleteFileW(path.c_str());
        return;
    }
    std::wofstream f(path, std::ios::trunc);
    for (const auto& [id, v] : journal_) f << v << L'\t' << id << L'\n';
}

} // namespace sq::app
