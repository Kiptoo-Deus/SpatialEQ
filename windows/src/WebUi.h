// The app's interface: the same Svelte UI as the macOS-matching VST3 editor (plugin/ui), in a WebView2
// child window. Maps the UI's parameter IDs (plugin/ui/src/lib/params.js) onto App::settings and sends
// the app's state (route, devices, presets, options) as window.sqApp.
#pragma once

#include "App.h"

#include <memory>

namespace sq::app {

class WebUi {
public:
    WebUi(App& app, void* parentHwnd);
    ~WebUi();
    WebUi(const WebUi&) = delete;
    WebUi& operator=(const WebUi&) = delete;

    void resize(int width, int height);
    /// Call about 30 times a second while the window is visible: sends changed settings, the app's
    /// state and a spectrum/meter frame.
    void tick();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace sq::app
