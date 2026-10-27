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

#include <cstdio>
#include <string>

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

    explicit Impl(SpatialEQPlugin* p) : plugin(p) {}
    ~Impl() {
        timer.clear();
        webview.reset();
        sq_spectrum_destroy(spectrum);
    }

    void create(void* parentView, int width, int height) {
        parent = parentView;
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
        webview = std::make_unique<choc::ui::WebView>(options);

        webview->bind("sqEdit", [this](const choc::value::ValueView& args) -> choc::value::Value {
            plugin->editPlain(ParamID(argNumber(args, 0)), argNumber(args, 1), argString(args, 2));
            return {};
        });
        webview->bind("sqCommand", [this](const choc::value::ValueView& args) -> choc::value::Value {
            const std::string name = argString(args, 0);
            if (name == "ready") ready = true;
            plugin->command(name, argNumber(args, 1), parent);
            return {};
        });

        embedChildView(parentView, webview->getViewHandle(), width, height);
        timer = choc::messageloop::Timer(33, [this] {
            tick();
            return true;
        });
    }

    void tick() {
        if (!webview || !ready) return;
        if (plugin->infoChanged.exchange(false)) {
            const double fs = plugin->sampleRate();
            const double latencyMs = 1000.0 * plugin->getLatencySamples() / fs;
            webview->evaluateJavascript("window.sqInfo({sampleRate:" + number(fs) + ",latencyMs:" + number(latencyMs) +
                                        ",version:\"" VERSION_STR "\",preset:" +
                                        choc::json::getEscapedQuotedString(plugin->presetName) + "})");
        }
        if (plugin->paramsChanged.exchange(false)) {
            std::string js = "window.sqParams({";
            for (const ParamDef& d : paramDefs())
                js += "\"" + std::to_string(d.id) + "\":" + number(toPlain(d, plugin->getParamNormalized(d.id))) + ",";
            js.back() = '}';
            webview->evaluateJavascript(js + ")");
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
    impl_->create(parent, rect.getWidth(), rect.getHeight());
    plugin_->paramsChanged = true;
    plugin_->infoChanged = true;
    return CPluginView::attached(parent, type);
}

tresult PLUGIN_API WebEditor::removed() {
    if (impl_ && impl_->webview) removeChildView(impl_->webview->getViewHandle());
    impl_.reset();
    return CPluginView::removed();
}

tresult PLUGIN_API WebEditor::onSize(ViewRect* newSize) {
    if (newSize) {
        rect = *newSize;
        if (impl_ && impl_->webview) resizeChildView(impl_->webview->getViewHandle(), rect.getWidth(), rect.getHeight());
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
