// The plug-in editor: a CPluginView hosting a choc WebView that runs the Svelte UI (plugin/ui).
#pragma once

#include "public.sdk/source/common/pluginview.h"

#include <memory>

namespace sq {

class SpatialEQPlugin;

class WebEditor : public Steinberg::CPluginView {
public:
    static constexpr int kWidth = 1280;
    static constexpr int kHeight = 820;
    static constexpr int kMinWidth = 980;
    static constexpr int kMinHeight = 680;

    explicit WebEditor(SpatialEQPlugin* plugin);
    ~WebEditor() override;

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) override;
    Steinberg::tresult PLUGIN_API removed() override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) override;
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SpatialEQPlugin* plugin_;
};

} // namespace sq
