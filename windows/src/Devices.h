// Render endpoint enumeration and change notifications (MMDevice API).
#pragma once

#include <atomic>
#include <string>
#include <vector>

struct IMMDeviceEnumerator;

namespace sq::app {

struct AudioDevice {
    std::wstring id;
    std::string name;        // UTF-8 friendly name, e.g. "Speakers (Realtek(R) Audio)"
    bool isDefault = false;  // the Windows default playback device
    bool isVirtual = false;  // a virtual cable / loopback driver (VB-CABLE, Voicemeeter, ...)
    bool headphoneLike = false;
};

class DeviceList {
public:
    DeviceList();
    ~DeviceList();
    DeviceList(const DeviceList&) = delete;
    DeviceList& operator=(const DeviceList&) = delete;

    // Re-reads the active render endpoints. Call from the UI thread.
    void refresh();
    const std::vector<AudioDevice>& devices() const { return devices_; }
    const AudioDevice* find(const std::wstring& id) const;
    const AudioDevice* defaultDevice() const;

    // Set by the notification client whenever devices are added/removed or the default changes.
    // `notifyWindow` (an HWND) gets a WM_APP+1 post so the UI wakes up.
    bool consumeChanged() { return changed_.exchange(false); }
    void setNotifyWindow(void* hwnd);

private:
    class Client;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    Client* client_ = nullptr;
    std::vector<AudioDevice> devices_;
    std::atomic<bool> changed_{false};
    std::atomic<void*> notifyWindow_{nullptr};
};

std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);

} // namespace sq::app
