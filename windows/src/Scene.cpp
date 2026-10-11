// Interactive 3D view of the listener and the virtual sources, drawn with ImGui's draw list and a
// small perspective camera. Same interactions as the macOS scene:
//   drag a source to move it, Shift-drag for height, drag empty space to orbit, scroll to zoom,
//   double-click to reset the view. In speaker mode, drag a speaker to set the span.
#include "App.h"
#include "Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace sq::app {

namespace {

constexpr float kPi = 3.14159265f;
constexpr float kDeg = kPi / 180.0f;

struct V3 {
    float x = 0, y = 0, z = 0;
};
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 normalize(V3 a) {
    const float l = std::sqrt(dot(a, a));
    return l > 0 ? a * (1.0f / l) : a;
}

// World: x right, y up, z towards the front of the listener. Listener's head at the origin.
V3 sourcePos(double azDeg, double elDeg, double dist) {
    const float az = float(azDeg) * kDeg, el = float(elDeg) * kDeg, d = float(dist);
    return {std::sin(az) * std::cos(el) * d, std::sin(el) * d, std::cos(az) * std::cos(el) * d};
}

struct Camera {
    float yaw = 0, pitch = 40 * kDeg, distance = 9.0f;
    float fov = 40 * kDeg;
    // derived per frame
    V3 eye, right, up, forward;
    ImVec2 center;
    float focal = 1;

    void update(ImVec2 origin, ImVec2 size) {
        eye = {distance * std::cos(pitch) * std::sin(yaw), distance * std::sin(pitch),
               -distance * std::cos(pitch) * std::cos(yaw)};
        forward = normalize(V3{0, -0.6f, 0.6f} - eye);
        right = normalize(cross({0, 1, 0}, forward));
        up = cross(forward, right);
        center = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
        focal = size.y * 0.5f / std::tan(fov * 0.5f);
    }
    // Returns false if behind the camera. `depth` is the view-space distance.
    bool project(V3 p, ImVec2& out, float& depth) const {
        const V3 v = p - eye;
        depth = dot(v, forward);
        if (depth < 0.1f) return false;
        out = ImVec2(center.x + dot(v, right) / depth * focal, center.y - dot(v, up) / depth * focal);
        return true;
    }
    float pixels(float metres, float depth) const { return metres / depth * focal; }
    V3 ray(ImVec2 screen) const {
        return normalize(forward + right * ((screen.x - center.x) / focal) + up * (-(screen.y - center.y) / focal));
    }
};

struct SceneState {
    Camera cam;
    int dragSource = -1;     // index into sources, or -1
    int dragSpeaker = -1;    // 0 / 1 in speaker mode
    bool orbiting = false;
    float pulse[SQ_MAX_SOURCES]{};
};
SceneState S;

ImU32 withAlpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(std::clamp(a, 0.0f, 1.0f) * 255) << 24); }

void floorRing(ImDrawList* dl, const Camera& cam, float radius, ImU32 color, float thickness) {
    constexpr int kSeg = 96;
    ImVec2 pts[kSeg + 1];
    int n = 0;
    for (int i = 0; i <= kSeg; ++i) {
        const float a = 2 * kPi * i / kSeg;
        float d;
        ImVec2 p;
        if (!cam.project({std::sin(a) * radius, 0, std::cos(a) * radius}, p, d)) {
            if (n > 1) dl->AddPolyline(pts, n, color, 0, thickness);
            n = 0;
            continue;
        }
        pts[n++] = p;
    }
    if (n > 1) dl->AddPolyline(pts, n, color, 0, thickness);
}

void glowDot(ImDrawList* dl, ImVec2 c, float r, ImU32 color, float glow) {
    for (int i = 4; i >= 1; --i) dl->AddCircleFilled(c, r * (1 + 0.45f * i * glow), withAlpha(color, 0.06f * glow), 32);
    dl->AddCircleFilled(c, r, color, 32);
    dl->AddCircleFilled(ImVec2(c.x - r * 0.28f, c.y - r * 0.3f), r * 0.38f, theme::rgba(1, 1, 1, 0.55f), 20);
}

void drawHead(ImDrawList* dl, const Camera& cam, float yawDeg) {
    float d;
    ImVec2 c;
    if (!cam.project({0, 0, 0}, c, d)) return;
    const float r = cam.pixels(0.34f, d);
    // Shaded sphere: concentric discs from dark rim to a highlight up and to the left.
    for (int i = 10; i >= 1; --i) {
        const float t = i / 10.0f;
        const float shade = 0.12f + 0.45f * (1 - t) * (1 - t) + 0.1f;
        dl->AddCircleFilled(ImVec2(c.x - r * 0.22f * (1 - t), c.y - r * 0.25f * (1 - t)), r * t,
                            theme::rgba(shade, shade + 0.01f, shade + 0.03f), 40);
    }
    // Nose marks the front.
    ImVec2 nose;
    const float a = yawDeg * kDeg;
    if (cam.project({std::sin(a) * 0.36f, 0, std::cos(a) * 0.36f}, nose, d))
        dl->AddCircleFilled(nose, cam.pixels(0.07f, d), theme::rgba(0.55f, 0.57f, 0.6f), 16);
    // Ears.
    for (int s = -1; s <= 1; s += 2) {
        ImVec2 ear;
        if (cam.project({s * 0.29f * std::cos(a), 0, -s * 0.29f * std::sin(a)}, ear, d))
            dl->AddCircleFilled(ear, cam.pixels(0.07f, d), theme::rgba(0.4f, 0.42f, 0.45f), 16);
    }
}

// Spectrum as a ring of bars around the listener, low frequencies at the front going clockwise.
void spectrumRing(ImDrawList* dl, const Camera& cam, const App& app) {
    const float* bands = app.spectrum();
    constexpr int kBars = 64;
    const float radius = 5.6f;
    for (int i = 0; i < kBars; ++i) {
        const int b = i * App::kSpectrumBands / kBars;
        const float v = bands[b];
        const float a = 2 * kPi * (i + 0.5f) / kBars;
        const V3 base{std::sin(a) * radius, 0, std::cos(a) * radius};
        const V3 top = base + V3{0, 0.05f + v * 1.6f, 0};
        ImVec2 p0, p1;
        float d0, d1;
        if (!cam.project(base, p0, d0) || !cam.project(top, p1, d1)) continue;
        const float t = float(i) / kBars;
        const ImU32 c = theme::rgba(theme::accent.x * (1 - t) + theme::accent2.x * t, theme::accent.y * (1 - t) + theme::accent2.y * t,
                                    theme::accent.z * (1 - t) + theme::accent2.z * t, 0.25f + 0.6f * v);
        dl->AddLine(p0, p1, c, std::max(1.5f, cam.pixels(0.16f, d0)));
    }
}

struct Drawable {
    float depth;
    int kind; // 0 source, 1 head, 2 speaker
    int index;
};

} // namespace

void drawScene(App& app, const UiFonts& fonts, ImVec2 size, float scale) {
    SoundSettings& s = app.settings;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##scene", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);

    Camera& cam = S.cam;
    cam.update(origin, size);

    // Background vignette.
    dl->AddRectFilledMultiColor(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme::rgba(0.06f, 0.07f, 0.10f),
                                theme::rgba(0.06f, 0.07f, 0.10f), theme::rgba(0.025f, 0.03f, 0.045f),
                                theme::rgba(0.025f, 0.03f, 0.045f));

    const bool speakers = s.mode == OutputMode::Speakers;
    const bool spatial = !speakers && s.spatialEnabled;
    const int sourceCount = spatial ? int(channelNames(s.upmix).size()) : 2;
    const auto names = channelNames(spatial ? s.upmix : Upmix::Stereo);

    // Floor: distance rings and spokes every 30 degrees.
    for (int r = 1; r <= 5; ++r) floorRing(dl, cam, float(r), theme::rgba(1, 1, 1, r == 5 ? 0.10f : 0.06f), 1.0f * scale);
    for (int k = 0; k < 12; ++k) {
        const float a = k * 30 * kDeg;
        ImVec2 p0, p1;
        float d;
        if (cam.project({std::sin(a) * 0.5f, 0, std::cos(a) * 0.5f}, p0, d) &&
            cam.project({std::sin(a) * 5.0f, 0, std::cos(a) * 5.0f}, p1, d))
            dl->AddLine(p0, p1, theme::rgba(1, 1, 1, k == 0 ? 0.14f : 0.04f), 1.0f * scale);
    }
    spectrumRing(dl, cam, app);

    // Positions for this frame.
    struct Item {
        V3 pos;
        ImVec2 screen;
        float depth = 0, radius = 0;
        bool visible = false;
    };
    std::vector<Item> items(SQ_MAX_SOURCES);
    const float level = app.level();
    for (int i = 0; i < SQ_MAX_SOURCES; ++i) S.pulse[i] += (level - S.pulse[i]) * 0.3f;

    Item spk[2];
    if (speakers) {
        for (int k = 0; k < 2; ++k) {
            const double az = (k == 0 ? -0.5 : 0.5) * s.speakerSpan;
            spk[k].pos = sourcePos(az, 0, 2.2) + V3{0, 0.1f, 0};
            spk[k].visible = cam.project(spk[k].pos, spk[k].screen, spk[k].depth);
            spk[k].radius = cam.pixels(0.28f, spk[k].depth);
        }
    } else {
        for (int i = 0; i < sourceCount; ++i) {
            const VirtualSource& v = spatial ? s.sources[i] : VirtualSource{i == 0 ? -30.0 : 30.0, 0, 1.5, 1};
            Item& it = items[i];
            it.pos = sourcePos(v.azimuth, v.elevation, v.distance);
            it.visible = cam.project(it.pos, it.screen, it.depth);
            it.radius = cam.pixels(0.2f * (0.85f + 0.5f * S.pulse[i]) * float(std::sqrt(std::max(0.2, v.gain))), it.depth);
        }
    }

    // ---- Interaction
    const ImVec2 mouse = io.MousePos;
    int hoverSource = -1, hoverSpeaker = -1;
    if (hovered) {
        float best = 1e9f;
        if (speakers) {
            for (int k = 0; k < 2; ++k) {
                const float d = std::hypot(mouse.x - spk[k].screen.x, mouse.y - spk[k].screen.y);
                if (spk[k].visible && d < std::max(spk[k].radius * 1.3f, 14 * scale) && d < best) { best = d; hoverSpeaker = k; }
            }
        } else if (spatial) {
            for (int i = 0; i < sourceCount; ++i) {
                const float d = std::hypot(mouse.x - items[i].screen.x, mouse.y - items[i].screen.y);
                if (items[i].visible && d < std::max(items[i].radius * 1.5f, 14 * scale) && d < best) { best = d; hoverSource = i; }
            }
        }
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        S.dragSource = hoverSource;
        S.dragSpeaker = hoverSpeaker;
        S.orbiting = hoverSource < 0 && hoverSpeaker < 0;
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) S.orbiting = true;
    if (!active) {
        S.dragSource = S.dragSpeaker = -1;
        S.orbiting = false;
    }
    if (active && S.orbiting) {
        cam.yaw += io.MouseDelta.x * 0.006f;
        cam.pitch = std::clamp(cam.pitch + io.MouseDelta.y * 0.005f, 5 * kDeg, 85 * kDeg);
    }
    if (hovered && io.MouseWheel != 0) cam.distance = std::clamp(cam.distance * std::pow(0.9f, io.MouseWheel), 3.5f, 16.0f);
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hoverSource < 0 && hoverSpeaker < 0) cam = Camera();

    auto floorHit = [&](float planeY, V3& hit) {
        const V3 dir = cam.ray(mouse);
        if (std::fabs(dir.y) < 1e-4f) return false;
        const float t = (planeY - cam.eye.y) / dir.y;
        if (t <= 0) return false;
        hit = cam.eye + dir * t;
        return true;
    };

    if (active && S.dragSource >= 0 && S.dragSource < sourceCount) {
        VirtualSource& v = s.sources[S.dragSource];
        if (io.KeyShift) {
            v.elevation = std::clamp(v.elevation - io.MouseDelta.y * 0.6, -80.0, 80.0);
        } else {
            const V3 cur = sourcePos(v.azimuth, v.elevation, v.distance);
            V3 hit;
            if (floorHit(cur.y, hit)) {
                const double rh = std::hypot(hit.x, hit.z);
                const double y = cur.y;
                v.azimuth = std::atan2(hit.x, hit.z) / kDeg;
                v.distance = std::clamp(std::hypot(rh, y), 0.3, 5.0);
                v.elevation = std::atan2(y, std::max(rh, 1e-3)) / kDeg;
            }
        }
    }
    if (active && S.dragSpeaker >= 0) {
        V3 hit;
        if (floorHit(0.1f, hit)) {
            const double az = std::fabs(std::atan2(hit.x, hit.z) / kDeg);
            s.speakerSpan = std::clamp(2 * az, 10.0, 120.0);
        }
    }

    // ---- Draw back to front.
    std::vector<Drawable> order;
    order.push_back({dot(V3{} - cam.eye, cam.forward), 1, 0});
    if (speakers) {
        for (int k = 0; k < 2; ++k)
            if (spk[k].visible) order.push_back({spk[k].depth, 2, k});
    } else {
        for (int i = 0; i < sourceCount; ++i)
            if (items[i].visible) order.push_back({items[i].depth, 0, i});
    }
    std::sort(order.begin(), order.end(), [](const Drawable& a, const Drawable& b) { return a.depth > b.depth; });

    ImGui::PushFont(fonts.caption);
    for (const Drawable& d : order) {
        if (d.kind == 1) {
            drawHead(dl, cam, 0);
            continue;
        }
        if (d.kind == 2) {
            const Item& it = spk[d.index];
            const float w = it.radius, h = it.radius * 1.5f;
            const ImVec2 c = it.screen;
            const bool hot = d.index == hoverSpeaker || d.index == S.dragSpeaker;
            dl->AddRectFilled(ImVec2(c.x - w * 0.6f, c.y - h * 0.6f), ImVec2(c.x + w * 0.6f, c.y + h * 0.6f),
                              theme::rgba(0.16f, 0.17f, 0.2f), 4 * scale);
            dl->AddRect(ImVec2(c.x - w * 0.6f, c.y - h * 0.6f), ImVec2(c.x + w * 0.6f, c.y + h * 0.6f),
                        hot ? theme::rgba(1, 1, 1, 0.8f) : theme::rgba(1, 1, 1, 0.15f), 4 * scale, 0, 1.5f * scale);
            dl->AddCircleFilled(ImVec2(c.x, c.y + h * 0.18f), w * 0.36f, theme::channelColor(d.index, 0.85f), 24);
            dl->AddCircleFilled(ImVec2(c.x, c.y - h * 0.3f), w * 0.14f, theme::rgba(0.4f, 0.42f, 0.46f), 16);
            continue;
        }
        const int i = d.index;
        const Item& it = items[i];
        // Stalk down to the floor for elevated sources, plus a shadow.
        ImVec2 fl;
        float fd;
        if (cam.project({it.pos.x, 0, it.pos.z}, fl, fd)) {
            dl->AddCircleFilled(fl, cam.pixels(0.1f, fd), theme::rgba(0, 0, 0, 0.35f), 20);
            if (std::fabs(it.pos.y) > 0.05f) dl->AddLine(fl, it.screen, theme::channelColor(i, 0.3f), 1.0f * scale);
        }
        const float alpha = spatial ? 1.0f : 0.45f;
        const bool hot = i == hoverSource || i == S.dragSource;
        glowDot(dl, it.screen, it.radius, theme::channelColor(i, alpha), spatial ? 0.6f + S.pulse[i] : 0.3f);
        if (hot) dl->AddCircle(it.screen, it.radius + 3 * scale, theme::rgba(1, 1, 1, 0.9f), 32, 1.5f * scale);
        const char* name = i < int(names.size()) ? names[i] : "";
        dl->AddText(ImVec2(it.screen.x + it.radius + 4 * scale, it.screen.y - it.radius - 6 * scale),
                    theme::rgba(1, 1, 1, 0.75f * alpha), name);
    }

    // Speaker mode: the widened stereo image as a faint arc between the speakers.
    if (speakers) {
        const float half = float(s.speakerSpan * 0.5 * std::min(s.width, 2.5));
        constexpr int kSeg = 32;
        ImVec2 pts[kSeg + 1];
        int n = 0;
        for (int k = 0; k <= kSeg; ++k) {
            float dd;
            ImVec2 p;
            if (cam.project(sourcePos(-half + 2 * half * k / kSeg, 0, 2.2) + V3{0, 0.1f, 0}, p, dd)) pts[n++] = p;
        }
        if (n > 1) dl->AddPolyline(pts, n, theme::rgba(theme::accent.x, theme::accent.y, theme::accent.z, 0.45f), 0, 2.0f * scale);
    }

    // Overlay: mode title and hint (SceneHint in MainView.swift).
    {
        std::string title, hint;
        if (speakers) {
            title = "Speakers";
            hint = "Drag a speaker to set the span - drag to orbit - scroll to zoom";
        } else if (spatial) {
            title = std::string("Headphones - ") + label(s.upmix);
            hint = "Drag sources to move them - Shift-drag for height - double-click resets view";
        } else {
            title = "Headphones - Stereo";
            hint = "Turn on Virtual surround to place sound around you";
        }
        const ImVec2 pad(10 * scale, 8 * scale);
        ImGui::PushFont(fonts.body);
        const ImVec2 ts = ImGui::CalcTextSize(title.c_str());
        ImGui::PopFont();
        const ImVec2 hs = ImGui::CalcTextSize(hint.c_str());
        const ImVec2 b0(origin.x + 12 * scale, origin.y + 12 * scale);
        const ImVec2 b1(b0.x + std::max(ts.x, hs.x) + pad.x * 2, b0.y + ts.y + hs.y + pad.y * 2 + 3 * scale);
        dl->AddRectFilled(b0, b1, theme::rgba(0, 0, 0, 0.35f), 8 * scale);
        ImGui::PushFont(fonts.body);
        dl->AddText(ImVec2(b0.x + pad.x, b0.y + pad.y), theme::rgba(1, 1, 1), title.c_str());
        ImGui::PopFont();
        dl->AddText(ImVec2(b0.x + pad.x, b0.y + pad.y + ts.y + 3 * scale), ImGui::GetColorU32(theme::textDim), hint.c_str());
    }
    ImGui::PopFont();

    // Tooltip for the source under the cursor.
    const int tip = S.dragSource >= 0 ? S.dragSource : hoverSource;
    if (tip >= 0 && tip < sourceCount) {
        const VirtualSource& v = s.sources[tip];
        ImGui::SetTooltip("%s  %+.0f\xC2\xB0  %.1f m  height %+.0f\xC2\xB0", names[tip], v.azimuth, v.distance, v.elevation);
    } else if (S.dragSpeaker >= 0 || hoverSpeaker >= 0) {
        ImGui::SetTooltip("Speaker span %.0f\xC2\xB0", s.speakerSpan);
    }
    dl->PopClipRect();
}

} // namespace sq::app
