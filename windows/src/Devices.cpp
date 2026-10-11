#include "Devices.h"

#include <windows.h>
#include <initguid.h> // defines the PKEY_* property keys used below in this translation unit
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>

#include <algorithm>
#include <cwctype>

namespace sq::app {

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// Forwards endpoint changes to the DeviceList. Callbacks arrive on an MMDevice worker thread.
class DeviceList::Client final : public IMMNotificationClient {
public:
    explicit Client(DeviceList* owner) : owner_(owner) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IMMNotificationClient)) {
            *out = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return signal(); }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return signal(); }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return signal(); }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        return flow == eRender && role == eConsole ? signal() : S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    void detach() { owner_ = nullptr; }

private:
    HRESULT signal() {
        if (DeviceList* o = owner_) {
            o->changed_.store(true);
            if (void* hwnd = o->notifyWindow_.load()) PostMessageW(static_cast<HWND>(hwnd), WM_APP + 1, 0, 0);
        }
        return S_OK;
    }

    std::atomic<DeviceList*> owner_;
    LONG refs_ = 1;
};

DeviceList::DeviceList() {
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                   reinterpret_cast<void**>(&enumerator_)))) {
        client_ = new Client(this);
        enumerator_->RegisterEndpointNotificationCallback(client_);
    }
    refresh();
}

DeviceList::~DeviceList() {
    if (enumerator_) {
        if (client_) {
            enumerator_->UnregisterEndpointNotificationCallback(client_);
            client_->detach();
            client_->Release();
        }
        enumerator_->Release();
    }
}

void DeviceList::setNotifyWindow(void* hwnd) { notifyWindow_.store(hwnd); }

namespace {

bool containsI(const std::wstring& hay, const wchar_t* needle) {
    std::wstring h = hay, n = needle;
    for (auto& c : h) c = wchar_t(std::towlower(c));
    for (auto& c : n) c = wchar_t(std::towlower(c));
    return h.find(n) != std::wstring::npos;
}

} // namespace

void DeviceList::refresh() {
    devices_.clear();
    if (!enumerator_) return;

    std::wstring defaultId;
    IMMDevice* def = nullptr;
    if (SUCCEEDED(enumerator_->GetDefaultAudioEndpoint(eRender, eConsole, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) {
            defaultId = id;
            CoTaskMemFree(id);
        }
        def->Release();
    }

    IMMDeviceCollection* coll = nullptr;
    if (FAILED(enumerator_->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &coll))) return;
    UINT count = 0;
    coll->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(coll->Item(i, &dev))) continue;
        AudioDevice d;
        LPWSTR id = nullptr;
        if (SUCCEEDED(dev->GetId(&id))) {
            d.id = id;
            CoTaskMemFree(id);
        }
        IPropertyStore* props = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            std::wstring name;
            if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = v.pwszVal;
            PropVariantClear(&v);
            if (SUCCEEDED(props->GetValue(PKEY_AudioEndpoint_FormFactor, &v)) && v.vt == VT_UI4)
                d.headphoneLike = v.ulVal == Headphones || v.ulVal == Headset;
            PropVariantClear(&v);
            d.name = toUtf8(name);
            d.isVirtual = containsI(name, L"cable") || containsI(name, L"vb-audio") || containsI(name, L"voicemeeter") ||
                          containsI(name, L"virtual");
            if (!d.headphoneLike) d.headphoneLike = containsI(name, L"headphone") || containsI(name, L"headset");
            props->Release();
        }
        d.isDefault = !defaultId.empty() && d.id == defaultId;
        devices_.push_back(std::move(d));
        dev->Release();
    }
    coll->Release();
    std::stable_sort(devices_.begin(), devices_.end(),
                     [](const AudioDevice& a, const AudioDevice& b) { return a.name < b.name; });
}

const AudioDevice* DeviceList::find(const std::wstring& id) const {
    for (const auto& d : devices_)
        if (d.id == id) return &d;
    return nullptr;
}

const AudioDevice* DeviceList::defaultDevice() const {
    for (const auto& d : devices_)
        if (d.isDefault) return &d;
    return nullptr;
}

} // namespace sq::app
