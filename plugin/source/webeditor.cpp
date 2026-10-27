#include "webeditor.h"

#include "filedialog.h"
#include "plugin.h"
#include "presets.h"
#include "spatialeq_spectrum.h"
#include "version.h"

#include "ui_html.h" // generated from plugin/ui/dist/index.html by CMake

#include "choc/gui/choc_MessageLoop.h"
#include "choc/gui/choc_WebView.h"
#include "choc/text/choc_JSON.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace Steinberg;

namespace sq {

namespace {

constexpr int kSpectrumBands = 64;

std::string number(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}

std::string argString(const choc::value::ValueView& args, uint32_t i) {
    if (!args.isArray() || args.size() <= i || !args[i].isString()) return {};
    return std::string(args[i].getString());
}

double argNumber(const choc::value::ValueView& args, uint32_t i) {
    if (!args.isArray() || args.size() <= i) return 0;
    return args[i].getWithDefault<double>(0);
}

} // namespace

struct WebEditor::Impl {
    SpatialEQPlugin* plugin;
    void* parent = nullptr;
    std::unique_ptr<choc::ui::WebView> webview;
    choc::messageloop::Timer timer;
    sq_spectrum* spectrum = sq_spectrum_create(kSpectrumBands);
    bool ready = false;
    std::vector<double> sent; // normalized values last sent to the UI
    int width = WebEditor::kWidth, height = WebEditor::kHeight;
    int ticks = 0;

    // Watchdog: if the page stops answering (its WebKit / WebView2 process died), load it again.
    using Clock = std::chrono::steady_clock;
    Clock::time_point lastReply = Clock::now();
    std::shared_ptr<int> generation = std::make_shared<int>(0); // guards replies from a replaced web view

    // Optional diagnostics: SPATIALEQ_UI_LOG=/path/to/file appends the UI's frame rate and render cost.
    std::string logPath = [] {
        const char* p = std::getenv("SPATIALEQ_UI_LOG");
        return std::string(p ? p : "");
    }();
    void log(const std::string& line) {
        if (logPath.empty()) return;
        if (FILE* f = std::fopen(logPath.c_str(), "a")) {
            std::fprintf(f, "%s\n", line.c_str());
            std::fclose(f);
        }
    }

    explicit Impl(SpatialEQPlugin* p) : plugin(p) {}
    ~Impl() {
        timer.clear();
        webview.reset();
        sq_spectrum_destroy(spectrum);
    }

    void create(void* parentView, int w, int h) {
        parent = parentView;
        width = w;
        height = h;
        createWebView();
        timer = choc::messageloop::Timer(33, [this] {
            tick();
            return true;
        });
    }

    void createWebView() {
        if (webview) removeChildView(webview->getViewHandle());
        webview.reset();
        ready = false;
        sent.clear();
        lastReply = Clock::now();
        ++*generation;

        choc::ui::WebView::Options options;
        options.acceptsFirstMouseClick = true;
#ifndef NDEBUG
        options.enableDebugMode = true;
#endif
        options.fetchResource = [](const std::string& path) -> std::optional<choc::ui::WebView::Options::Resource> {
            if (path == "/" || path == "/index.html")
                return choc::ui::WebView::Options::Resource(
                    std::string_view(reinterpret_cast<const char*>(kUiHtml), sizeof kUiHtml), "text/html");
            return {};
        };
        // On Windows, WebView2 starts asynchronously and bind() fails until it's ready (on macOS this
        // runs during construction, before `webview` is assigned, hence the parameter).
        options.webviewIsReady = [this](choc::ui::WebView& view) {
            view.bind("sqEdit", [this](const choc::value::ValueView& args) -> choc::value::Value {
                plugin->editPlain(ParamID(argNumber(args, 0)), argNumber(args, 1), argString(args, 2));
                return {};
            });
            view.bind("sqCommand", [this](const choc::value::ValueView& args) -> choc::value::Value {
                const std::string name = argString(args, 0);
                if (name == "ready") {
                    ready = true;
                    sent.clear(); // a (re)loaded page needs every value
                    log("page connected");
                }
                plugin->command(name, argNumber(args, 1), parent);
                return {};
            });
        };
        webview = std::make_unique<choc::ui::WebView>(options);

        embedChildView(parent, webview->getViewHandle(), width, height);
        log("web view created");
    }

    void ping() {
        std::weak_ptr<int> gen = generation;
        const int expected = *generation;
        webview->evaluateJavascript(logPath.empty() ? "1" : "window.sqStats ? sqStats() : ''",
                                    [this, gen, expected](const std::string& error, const choc::value::ValueView& v) {
            const auto g = gen.lock();
            if (!g || *g != expected) return;
            lastReply = Clock::now();
            if (!error.empty()) log("js error: " + error);
            else if (v.isString()) log(std::string(v.getString()));
        });
    }

    void tick() {
        if (!webview) return;
        if (++ticks % 60 == 0) { // every 2 s
            if (ready && Clock::now() - lastReply > std::chrono::seconds(6)) {
                log("page not responding, reloading");
                createWebView();
                return;
            }
            ping();
        }
        if (!ready) return;
        if (plugin->infoChanged.exchange(false)) {
            const double fs = plugin->sampleRate();
            const double latencyMs = 1000.0 * plugin->getLatencySamples() / fs;
            webview->evaluateJavascript("window.sqInfo({sampleRate:" + number(fs) + ",latencyMs:" + number(latencyMs) +
                                        ",version:\"" VERSION_STR "\",preset:" +
                                        choc::json::getEscapedQuotedString(plugin->presetName) + "})");
        }
        if (plugin->paramsChanged.exchange(false)) {
            // Only the values that changed since the last update (all of them the first time).
            const auto& defs = paramDefs();
            sent.resize(defs.size(), -1.0);
            std::string js;
            for (size_t i = 0; i < defs.size(); ++i) {
                const double n = plugin->getParamNormalized(defs[i].id);
                if (n == sent[i]) continue;
                sent[i] = n;
                js += (js.empty() ? "" : ",") + ("\"" + std::to_string(defs[i].id) + "\":" + number(toPlain(defs[i], n)));
            }
            if (!js.empty()) webview->evaluateJavascript("window.sqParams({" + js + "})");
        }

        float bands[kSpectrumBands];
        const float level = sq_spectrum_update(spectrum, plugin->engine(), plugin->sampleRate(), bands);
        sq_meters m{};
        sq_engine_get_meters(plugin->engine(), &m);
        std::string js = "window.sqFrame({bands:[";
        for (int i = 0; i < kSpectrumBands; ++i) js += (i ? "," : "") + number(bands[i]);
        js += "],level:" + number(level) + ",peakL:" + number(m.peakL) + ",peakR:" + number(m.peakR) +
              ",lim:" + number(m.limiterReductionDb) + ",lev:" + number(m.levelerGainDb) + "})";
        webview->evaluateJavascript(js);
    }
};

WebEditor::WebEditor(SpatialEQPlugin* plugin) : CPluginView(nullptr), plugin_(plugin) {
    ViewRect r(0, 0, kWidth, kHeight);
    setRect(r);
    plugin_->addRef();
}

WebEditor::~WebEditor() {
    impl_.reset();
    plugin_->release();
}

tresult PLUGIN_API WebEditor::isPlatformTypeSupported(FIDString type) {
#if defined(__APPLE__)
    return FIDStringsEqual(type, kPlatformTypeNSView) ? kResultTrue : kResultFalse;
#elif defined(_WIN32)
    return FIDStringsEqual(type, kPlatformTypeHWND) ? kResultTrue : kResultFalse;
#else
    (void)type;
    return kResultFalse;
#endif
}

tresult PLUGIN_API WebEditor::attached(void* parent, FIDString type) {
    if (isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
    impl_ = std::make_unique<Impl>(plugin_);
    impl_->log("attached " + std::to_string(rect.getWidth()) + "x" + std::to_string(rect.getHeight()));
    impl_->create(parent, rect.getWidth(), rect.getHeight());
    plugin_->paramsChanged = true;
    plugin_->infoChanged = true;
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API WebEditor::removed() {
    if (impl_) impl_->log("removed");
    if (impl_ && impl_->webview) removeChildView(impl_->webview->getViewHandle());
    impl_.reset();
    return CPluginView::removed();
}

tresult PLUGIN_API WebEditor::onSize(ViewRect* newSize) {
    if (newSize) {
        rect = *newSize;
        if (impl_ && impl_->webview) {
            impl_->width = rect.getWidth();
            impl_->height = rect.getHeight();
            resizeChildView(impl_->webview->getViewHandle(), rect.getWidth(), rect.getHeight());
        }
    }
    return kResultOk;
}

tresult PLUGIN_API WebEditor::checkSizeConstraint(ViewRect* r) {
    if (!r) return kInvalidArgument;
    if (r->getWidth() < kMinWidth) r->right = r->left + kMinWidth;
    if (r->getHeight() < kMinHeight) r->bottom = r->top + kMinHeight;
    return kResultTrue;
}

} // namespace sq
