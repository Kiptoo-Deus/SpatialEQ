// Dear ImGui user interface: top bar, 3D scene, controls sidebar and parametric EQ editor.
// Layout and wording follow the macOS app (macos/App/UI).
#pragma once

#include "imgui.h"

#include <string>

namespace sq::app {

class App;

struct UiFonts {
    ImFont* body = nullptr;
    ImFont* caption = nullptr;
    ImFont* title = nullptr;
};

namespace theme {
constexpr ImU32 rgba(float r, float g, float b, float a = 1.0f) {
    return ImU32(int(r * 255 + 0.5f)) | ImU32(int(g * 255 + 0.5f)) << 8 | ImU32(int(b * 255 + 0.5f)) << 16 |
           ImU32(int(a * 255 + 0.5f)) << 24;
}
constexpr ImVec4 vec(float r, float g, float b, float a = 1.0f) { return ImVec4(r, g, b, a); }

inline const ImVec4 background = vec(0.035f, 0.04f, 0.06f);
inline const ImVec4 panel = vec(0.07f, 0.08f, 0.11f);
inline const ImVec4 accent = vec(0.36f, 0.85f, 0.95f);
inline const ImVec4 accent2 = vec(0.98f, 0.62f, 0.27f);
inline const ImVec4 textDim = vec(1, 1, 1, 0.55f);
inline const ImVec4 warning = vec(1.0f, 0.62f, 0.25f);

ImU32 channelColor(int index, float alpha = 1.0f);
} // namespace theme

void setupStyle(float scale);

// Draws the whole window. `hwnd` owns file dialogs.
void drawUi(App& app, const UiFonts& fonts, float scale, void* hwnd);

// The interactive 3D view (Scene.cpp), drawn into the current window at the cursor.
void drawScene(App& app, const UiFonts& fonts, ImVec2 size, float scale);

} // namespace sq::app
