#include "AudioRouter.h"

#include <windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <avrt.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sq::app {

// -------------------------------------------------------------------------------------------------
// StereoRing / format conversion

int StereoRing::write(const float* l, const float* r, int n) {
    n = std::min(n, space());
    const uint64_t cap = uint64_t(capacity());
    for (int i = 0; i < n; ++i) {
        const size_t k = size_t((head_ + uint64_t(i)) % cap);
        l_[k] = l[i];
        r_[k] = r[i];
    }
    head_ += uint64_t(n);
    return n;
}

int StereoRing::readInterleaved(float* dst, int outCh, int n) {
    n = std::min(n, available());
    const uint64_t cap = uint64_t(capacity());
    for (int i = 0; i < n; ++i) {
        const size_t k = size_t((tail_ + uint64_t(i)) % cap);
        float* frame = dst + size_t(i) * size_t(outCh);
        frame[0] = l_[k];
        if (outCh > 1) frame[1] = r_[k];
        for (int c = 2; c < outCh; ++c) frame[c] = 0.0f;
    }
    tail_ += uint64_t(n);
    return n;
}

bool deinterleave(const void* src, const SampleFormat& fmt, int frames, float* l, float* r) {
    const int ch = fmt.channels;
    if (ch < 1) return false;
    const int rc = ch > 1 ? 1 : 0;
    if (fmt.isFloat) {
        if (fmt.bitsPerSample != 32) return false;
        const float* s = static_cast<const float*>(src);
        for (int i = 0; i < frames; ++i) {
            l[i] = s[size_t(i) * ch];
            r[i] = s[size_t(i) * ch + rc];
        }
        return true;
    }
    switch (fmt.bitsPerSample) {
    case 16: {
        const int16_t* s = static_cast<const int16_t*>(src);
        for (int i = 0; i < frames; ++i) {
            l[i] = s[size_t(i) * ch] * (1.0f / 32768.0f);
            r[i] = s[size_t(i) * ch + rc] * (1.0f / 32768.0f);
        }
        return true;
    }
    case 24: {
        const uint8_t* s = static_cast<const uint8_t*>(src);
        auto at = [&](size_t idx) {
            const uint8_t* p = s + idx * 3;
            const int32_t v = int32_t(uint32_t(p[0]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 24) >> 8;
            return v * (1.0f / 8388608.0f);
        };
        for (int i = 0; i < frames; ++i) {
            l[i] = at(size_t(i) * ch);
            r[i] = at(size_t(i) * ch + rc);
        }
        return true;
    }
    case 32: {
        const int32_t* s = static_cast<const int32_t*>(src);
        for (int i = 0; i < frames; ++i) {
            l[i] = float(s[size_t(i) * ch] * (1.0 / 2147483648.0));
            r[i] = float(s[size_t(i) * ch + rc] * (1.0 / 2147483648.0));
        }
        return true;
    }
    default:
        return false;
    }
}

float DuckCompensation::gainFor(const float* l, const float* r, int frames, double rate) {
    float peak = 0;
    for (int i = 0; i < frames; ++i) peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    if (peak > kOverThreshold) {
        // Something plays at full level, i.e. an app that is not ducked yet (just started, or its
        // volume was reset). Boosting that by 80 dB would be dangerous: pass everything through as
        // is until it has been quiet for a moment.
        holdFrames_ = int(0.3 * rate);
        return 1.0f;
    }
    if (holdFrames_ > 0) {
        holdFrames_ -= frames;
        return 1.0f;
    }
    return 1.0f / kDuckLevel;
}

bool processLoopbackSupported() {
    // Process loopback capture arrived in Windows 10 version 2004 (build 19041).
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const bool supported = [] {
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        auto fn = ntdll ? reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
        OSVERSIONINFOW v{sizeof(v)};
        if (!fn || fn(&v) != 0) return false;
        return v.dwMajorVersion > 10 || (v.dwMajorVersion == 10 && v.dwBuildNumber >= 19041);
    }();
    return supported;
}

// -------------------------------------------------------------------------------------------------
// AudioRouter

namespace {

template <typename T>
struct ComPtr {
    T* p = nullptr;
    ~ComPtr() { reset(); }
    void reset() {
        if (p) p->Release();
        p = nullptr;
    }
    T** put() {
        reset();
        return &p;
    }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

struct CoTaskFormat {
    WAVEFORMATEX* p = nullptr;
    ~CoTaskFormat() { CoTaskMemFree(p); }
};

SampleFormat describe(const WAVEFORMATEX* wf) {
    SampleFormat f;
    f.channels = wf->nChannels;
    f.bitsPerSample = wf->wBitsPerSample;
    f.validBits = wf->wBitsPerSample;
    f.isFloat = wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22) {
        const auto* ext = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(wf);
        f.isFloat = ext->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        f.validBits = ext->Samples.wValidBitsPerSample ? ext->Samples.wValidBitsPerSample : wf->wBitsPerSample;
        if (!f.isFloat && ext->SubFormat != KSDATAFORMAT_SUBTYPE_PCM) f.bitsPerSample = 0; // unsupported
    } else if (wf->wFormatTag != WAVE_FORMAT_PCM && wf->wFormatTag != WAVE_FORMAT_IEEE_FLOAT) {
        f.bitsPerSample = 0;
    }
    return f;
}

std::string hrText(const char* what, HRESULT hr) {
    char buf[160];
    if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
        std::snprintf(buf, sizeof(buf), "%s: the device was removed or reconfigured", what);
    else if (hr == AUDCLNT_E_DEVICE_IN_USE)
        std::snprintf(buf, sizeof(buf), "%s: the device is in exclusive use by another app", what);
    else
        std::snprintf(buf, sizeof(buf), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return buf;
}

// Completion handler for ActivateAudioInterfaceAsync (must be agile; called on a worker thread).
class ActivationHandler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivationHandler() : done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivationHandler() { CloseHandle(done_); }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refs_); }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG r = InterlockedDecrement(&refs_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** out) override {
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
        } else if (iid == __uuidof(IAgileObject)) {
            *out = static_cast<IAgileObject*>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        IUnknown* unknown = nullptr;
        HRESULT activateHr = E_FAIL;
        if (SUCCEEDED(op->GetActivateResult(&activateHr, &unknown)) && SUCCEEDED(activateHr) && unknown) {
            result_ = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void**>(&client_));
        } else {
            result_ = FAILED(activateHr) ? activateHr : E_FAIL;
        }
        if (unknown) unknown->Release();
        SetEvent(done_);
        return S_OK;
    }
    HRESULT wait(IAudioClient** out) {
        if (WaitForSingleObject(done_, 5000) != WAIT_OBJECT_0) return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        *out = client_;
        client_ = nullptr;
        return result_;
    }

private:
    HANDLE done_;
    LONG refs_ = 1;
    HRESULT result_ = E_FAIL;
    IAudioClient* client_ = nullptr;
};

// Captures every process except this one (and its children).
HRESULT activateProcessLoopback(IAudioClient** out) {
    AUDIOCLIENT_ACTIVATION_PARAMS params{};
    params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId = GetCurrentProcessId();
    params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT pv{};
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(params);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&params);
    auto* handler = new ActivationHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &pv,
                                             handler, &op);
    if (SUCCEEDED(hr)) hr = handler->wait(out);
    if (op) op->Release();
    handler->Release();
    return hr;
}

constexpr REFERENCE_TIME kCaptureBuffer = 1000000; // 100 ms: loopback packets are drained every period
constexpr REFERENCE_TIME kRenderBuffer = 400000;   // 40 ms, of which we keep ~2 device periods queued

} // namespace

void AudioRouter::start(const std::wstring& sourceId, const std::wstring& outputId) {
    stop();
    Status s;
    s.state = State::Starting;
    setStatus(s);
    running_.store(true);
    thread_ = std::thread(&AudioRouter::run, this, sourceId, outputId);
}

void AudioRouter::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    Status s = status();
    if (s.state != State::Failed) setStatus({});
}

AudioRouter::Status AudioRouter::status() const {
    std::lock_guard<std::mutex> lock(statusMutex_);
    return status_;
}

void AudioRouter::setStatus(const Status& s) {
    std::lock_guard<std::mutex> lock(statusMutex_);
    status_ = s;
}

void AudioRouter::fail(const std::string& message, bool restart) {
    Status s;
    s.state = State::Failed;
    s.message = message;
    setStatus(s);
    if (restart) needsRestart_.store(true);
}

void AudioRouter::run(std::wstring sourceId, std::wstring outputId) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return fail("COM initialisation failed", false);
    struct ComScope { ~ComScope() { CoUninitialize(); } } comScope;

    if (sourceId.empty() || outputId.empty()) return fail("Choose a source and an output device", false);
    const bool allApps = sourceId == kAllAppsSource;
    if (allApps && !processLoopbackSupported())
        return fail("Capturing all apps needs Windows 10 version 2004 or later; choose a source device instead", false);
    if (sourceId == outputId)
        return fail("Source and output are the same device; pick a different output to avoid feedback", false);

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                                  reinterpret_cast<void**>(enumerator.put()));
    if (FAILED(hr)) return fail(hrText("Opening the audio device list", hr), false);

    ComPtr<IMMDevice> outDev;
    if (FAILED(hr = enumerator->GetDevice(outputId.c_str(), outDev.put())))
        return fail(hrText("Opening the output device", hr), false);
    ComPtr<IAudioClient> outClient;
    if (FAILED(hr = outDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                     reinterpret_cast<void**>(outClient.put()))))
        return fail(hrText("Activating the output device", hr), true);
    CoTaskFormat outMix;
    if (FAILED(hr = outClient->GetMixFormat(&outMix.p))) return fail(hrText("Reading the output format", hr), true);

    // ---- Capture
    ComPtr<IAudioClient> capClient;
    SampleFormat inFmt;
    int inBlockAlign = 8;
    double rate = 0;
    HANDLE capEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr); // process loopback requires one; we poll
    struct CapEventScope { HANDLE h; ~CapEventScope() { CloseHandle(h); } } capEventScope{capEvent};
    if (allApps) {
        // Every process except SpatialEQ, as float stereo at the output's rate.
        if (FAILED(hr = activateProcessLoopback(capClient.put())))
            return fail(hrText("Starting app capture", hr), true);
        rate = outMix.p->nSamplesPerSec;
        WAVEFORMATEX f{};
        f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        f.nChannels = 2;
        f.nSamplesPerSec = DWORD(rate);
        f.wBitsPerSample = 32;
        f.nBlockAlign = 8;
        f.nAvgBytesPerSec = f.nSamplesPerSec * f.nBlockAlign;
        inFmt = {2, 32, 32, true};
        if (FAILED(hr = capClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                              AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                                                  AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                              kCaptureBuffer, 0, &f, nullptr)))
            return fail(hrText("Starting app capture", hr), true);
        capClient->SetEventHandle(capEvent);
    } else {
        // Loopback of the source endpoint, in its own mix format.
        ComPtr<IMMDevice> srcDev;
        if (FAILED(hr = enumerator->GetDevice(sourceId.c_str(), srcDev.put())))
            return fail(hrText("Opening the source device", hr), false);
        if (FAILED(hr = srcDev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(capClient.put()))))
            return fail(hrText("Activating the source device", hr), true);
        CoTaskFormat capFmt;
        if (FAILED(hr = capClient->GetMixFormat(&capFmt.p))) return fail(hrText("Reading the source format", hr), true);
        inFmt = describe(capFmt.p);
        if (inFmt.bitsPerSample == 0) return fail("The source device uses an unsupported sample format", false);
        inBlockAlign = capFmt.p->nBlockAlign;
        rate = capFmt.p->nSamplesPerSec;
        if (FAILED(hr = capClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, kCaptureBuffer, 0,
                                              capFmt.p, nullptr)))
            return fail(hrText("Starting loopback capture", hr), true);
    }
    ComPtr<IAudioCaptureClient> capture;
    if (FAILED(hr = capClient->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture.put()))))
        return fail(hrText("Starting capture", hr), true);

    // ---- Render: float at the capture rate, with the output's channel layout. Windows resamples
    // to the output's own mix rate if it differs (AUTOCONVERTPCM).
    const int outCh = std::max<int>(1, outMix.p->nChannels);
    WAVEFORMATEXTENSIBLE wf{};
    wf.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wf.Format.nChannels = WORD(outCh);
    wf.Format.nSamplesPerSec = DWORD(rate);
    wf.Format.wBitsPerSample = 32;
    wf.Format.nBlockAlign = WORD(outCh * 4);
    wf.Format.nAvgBytesPerSec = wf.Format.nSamplesPerSec * wf.Format.nBlockAlign;
    wf.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wf.Samples.wValidBitsPerSample = 32;
    wf.dwChannelMask = outMix.p->wFormatTag == WAVE_FORMAT_EXTENSIBLE
                           ? reinterpret_cast<WAVEFORMATEXTENSIBLE*>(outMix.p)->dwChannelMask
                           : (outCh == 1 ? SPEAKER_FRONT_CENTER : SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    wf.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;

    DWORD renderFlags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (DWORD(rate) != outMix.p->nSamplesPerSec)
        renderFlags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (FAILED(hr = outClient->Initialize(AUDCLNT_SHAREMODE_SHARED, renderFlags, kRenderBuffer, 0,
                                          reinterpret_cast<WAVEFORMATEX*>(&wf), nullptr)))
        return fail(hrText("Opening the output device", hr), true);
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    struct EventScope { HANDLE h; ~EventScope() { CloseHandle(h); } } eventScope{event};
    outClient->SetEventHandle(event);
    ComPtr<IAudioRenderClient> render;
    if (FAILED(hr = outClient->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render.put()))))
        return fail(hrText("Opening the output device", hr), true);

    UINT32 renderBufferFrames = 0;
    outClient->GetBufferSize(&renderBufferFrames);
    REFERENCE_TIME defaultPeriod = 100000, minPeriod = 0;
    outClient->GetDevicePeriod(&defaultPeriod, &minPeriod);
    const int period = std::max(32, int(double(defaultPeriod) * 1e-7 * rate + 0.5));
    // Normally about two periods sit at the device; anything beyond latencyCap (device + FIFO) is
    // clock drift and gets dropped.
    const int targetPadding = std::min<int>(int(renderBufferFrames), 2 * period);
    const int latencyCap = std::min<int>(int(renderBufferFrames), 4 * period);

    // ---- DSP
    constexpr int kBlock = 2048;
    sq_engine_prepare(engine_, rate, kBlock);
    std::vector<float> l(kBlock), r(kBlock);
    DuckCompensation compensation;
    StereoRing fifo;
    fifo.allocate(16 * period + kBlock);

    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

    // Pre-fill with silence so the first periods do not underrun.
    BYTE* out = nullptr;
    if (SUCCEEDED(render->GetBuffer(UINT32(targetPadding), &out)))
        render->ReleaseBuffer(UINT32(targetPadding), AUDCLNT_BUFFERFLAGS_SILENT);

    if (FAILED(hr = outClient->Start())) return fail(hrText("Starting the output device", hr), true);
    if (FAILED(hr = capClient->Start())) {
        outClient->Stop();
        return fail(hrText("Starting loopback capture", hr), true);
    }

    {
        Status s;
        s.state = State::Running;
        s.sampleRate = rate;
        s.outputChannels = outCh;
        // One loopback packet plus what is queued at the output.
        s.latencyMs = double(period + targetPadding) / rate * 1000.0;
        setStatus(s);
    }

    std::string error;
    while (running_.load()) {
        const DWORD w = WaitForSingleObject(event, 200);
        if (w != WAIT_OBJECT_0 && w != WAIT_TIMEOUT) {
            error = "Waiting for the output device failed";
            break;
        }

        // Drain every loopback packet through the engine into the FIFO.
        UINT32 packet = 0;
        while (SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            for (UINT32 done = 0; done < frames;) {
                const int n = int(std::min<UINT32>(frames - done, kBlock));
                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    std::fill(l.begin(), l.begin() + n, 0.0f);
                    std::fill(r.begin(), r.begin() + n, 0.0f);
                } else {
                    deinterleave(data + size_t(done) * size_t(inBlockAlign), inFmt, n, l.data(), r.data());
                }
                if (allApps) {
                    // Undo the mixer duck (see Ducker.h), unless un-ducked audio is present.
                    const float g = compensation.gainFor(l.data(), r.data(), n, rate);
                    for (int i = 0; i < n; ++i) {
                        l[i] *= g;
                        r[i] *= g;
                    }
                }
                sq_engine_process(engine_, l.data(), r.data(), n);
                fifo.write(l.data(), r.data(), n);
                done += UINT32(n);
            }
            capture->ReleaseBuffer(frames);
        }
        if (FAILED(hr)) {
            error = hrText("Loopback capture", hr);
            break;
        }

        UINT32 padding = 0;
        if (FAILED(hr = outClient->GetCurrentPadding(&padding))) {
            error = hrText("Output", hr);
            break;
        }

        // The two devices run on separate clocks. If the source runs fast, audio piles up: drop the
        // excess rather than letting latency creep up.
        if (int(padding) + fifo.available() > latencyCap) fifo.trimTo(std::max(0, targetPadding - int(padding)));

        // Device nearly dry and nothing to give it (source silent, or slower than the output):
        // queue one period of silence as a cushion so the next packets play without crackling.
        const int space = int(renderBufferFrames) - int(padding);
        int silence = 0;
        if (int(padding) < period / 2 && fifo.available() < period) silence = std::min(period, space);

        const int n = std::min(fifo.available(), space - silence);
        const int total = silence + std::max(0, n);
        if (total <= 0) continue;
        if (FAILED(hr = render->GetBuffer(UINT32(total), &out))) {
            error = hrText("Output", hr);
            break;
        }
        float* dst = reinterpret_cast<float*>(out);
        std::fill(dst, dst + size_t(silence) * outCh, 0.0f);
        fifo.readInterleaved(dst + size_t(silence) * outCh, outCh, std::max(0, n));
        render->ReleaseBuffer(UINT32(total), 0);
    }

    capClient->Stop();
    outClient->Stop();
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    if (!error.empty()) fail(error, true);
}

} // namespace sq::app
