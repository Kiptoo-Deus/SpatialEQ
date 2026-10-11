// "All apps" mode support: turns every other app's audio session right down in the Windows volume
// mixer while SpatialEQ plays their (captured, compensated) sound itself, and puts them back after.
//
// Process loopback capture happens after the per-app mixer volume, so an app at volume v * kDuckLevel
// is captured at exactly kDuckLevel of its normal level (the mixer is a floating-point multiply) and
// the router multiplies by 1 / kDuckLevel to restore it. What still leaks to the speakers directly is
// 80 dB down, which is inaudible under the processed signal.
//
// Only sessions that are playing are ducked; a session that pauses gets its volume back at once.
// That matters because Windows remembers each app's volume, and a change made after an app has
// exited does not stick: apps stop their audio before quitting, so they leave at their own level.
// Original volumes are journaled to %APPDATA%\SpatialEQ\ducked.txt while sessions are ducked, so an
// app that exits mid-playback, or a crash, is put right the next time SpatialEQ sees the app.
#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

struct IAudioSessionManager2;

namespace sq::app {

constexpr float kDuckLevel = 1e-4f; // -80 dB

// Is a session volume one we set (i.e. ducked), given the app's original volume?
inline bool looksDucked(float volume, float original) {
    return volume <= original * kDuckLevel * 1.5f + 1e-7f;
}

class SessionDucker {
public:
    SessionDucker();
    ~SessionDucker();
    SessionDucker(const SessionDucker&) = delete;
    SessionDucker& operator=(const SessionDucker&) = delete;

    // Ducks every other app's session on every active playback device, and keeps doing so for new
    // sessions until stop(). Call from the UI thread.
    void start();
    // Restores every session ducked by this instance and clears the journal.
    void stop();
    bool active() const { return active_; }

    // Re-scans sessions: ducks new ones, follows user volume changes, restores journaled sessions.
    // Cheap; call a few times per second from the UI thread.
    void poll();

    // Number of sessions currently ducked (for the UI).
    int duckedCount() const;

    // Safety net for the user: puts every app at or below the duck level back to its journaled
    // volume (or 100 %). Returns how many were fixed.
    int resetAll();

private:
    struct Entry {
        std::wstring identifier; // stable per app (GetSessionInstanceIdentifier for this run)
        std::wstring appId;      // GetSessionIdentifier: stable across runs, used by the journal
        float original = 1.0f;
        void* volume = nullptr;  // ISimpleAudioVolume*
        void* control = nullptr; // IAudioSessionControl*
        bool seen = false;
    };

    void acquireManagers();
    void scan(bool duck);
    // `persisted`: the session is still alive, so Windows keeps the restored volume for the app and
    // the journal entry can go. After the app has exited the change does not stick.
    void restore(Entry& e, bool persisted);
    void loadJournal();
    void saveJournal();

    std::vector<IAudioSessionManager2*> managers_;
    std::map<std::wstring, Entry> entries_;     // by session instance identifier
    std::map<std::wstring, float> journal_;     // appId -> original volume (persisted)
    mutable std::mutex mutex_;
    bool active_ = false;
    unsigned long selfPid_ = 0;
};

} // namespace sq::app
