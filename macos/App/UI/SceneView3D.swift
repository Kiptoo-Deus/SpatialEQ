import SceneKit
import SwiftUI

/// Interactive 3D view: a listener's head, draggable virtual sources around it, and a spectrum
/// terrain + EQ ribbon driven by live audio analysis.
struct SceneView3D: NSViewRepresentable {
    @EnvironmentObject var state: AppState
    @EnvironmentObject var tracker: HeadTracker

    func makeCoordinator() -> SceneController {
        SceneController(analyzer: state.analyzer)
    }

    func makeNSView(context: Context) -> OrbitSceneView {
        let view = OrbitSceneView(frame: .zero)
        let controller = context.coordinator
        view.scene = controller.scene
        view.pointOfView = controller.cameraNode
        view.delegate = controller
        view.backgroundColor = NSColor(Theme.background)
        view.antialiasingMode = .multisampling4X
        view.rendersContinuously = true
        view.preferredFramesPerSecond = 60
        view.controller = controller
        controller.onSourceMoved = { [weak state] index, az, el, dist in
            DispatchQueue.main.async { state?.setSource(index, azimuth: az, elevation: el, distance: dist) }
        }
        controller.onSpeakerSpanChanged = { [weak state] span in
            DispatchQueue.main.async { state?.settings.speakerSpan = span }
        }
        return view
    }

    func updateNSView(_ view: OrbitSceneView, context: Context) {
        context.coordinator.update(settings: state.settings, enabled: state.enabled, headYaw: tracker.yawDegrees)
    }
}

// MARK: - View with orbit camera and source dragging

final class OrbitSceneView: SCNView {
    weak var controller: SceneController?
    private enum Drag { case none, orbit, source(Int), speaker(Int) }
    private var drag: Drag = .none
    private var lastPoint: NSPoint = .zero

    override var acceptsFirstResponder: Bool { true }

    override func mouseDown(with event: NSEvent) {
        let p = convert(event.locationInWindow, from: nil)
        lastPoint = p
        if event.clickCount == 2 {
            controller?.resetCamera()
            return
        }
        let hits = hitTest(p, options: [.searchMode: SCNHitTestSearchMode.all.rawValue, .ignoreHiddenNodes: true])
        for hit in hits {
            var node: SCNNode? = hit.node
            while let n = node {
                if let name = n.name, name.hasPrefix("source-"), let i = Int(name.dropFirst(7)) {
                    drag = .source(i)
                    controller?.draggingIndex = i
                    return
                }
                if let name = n.name, name.hasPrefix("speaker-"), let i = Int(name.dropFirst(8)) {
                    drag = .speaker(i)
                    return
                }
                node = n.parent
            }
        }
        drag = .orbit
    }

    override func mouseDragged(with event: NSEvent) {
        guard let controller else { return }
        let p = convert(event.locationInWindow, from: nil)
        defer { lastPoint = p }
        switch drag {
        case .orbit:
            var e = controller.rig.eulerAngles
            e.y -= CGFloat(p.x - lastPoint.x) * 0.008
            e.x = max(-1.45, min(0.2, e.x + CGFloat(p.y - lastPoint.y) * 0.008))
            controller.rig.eulerAngles = e
        case let .source(i):
            let s = controller.currentSettings()
            guard s.sources.indices.contains(i) else { return }
            var src = s.sources[i]
            if event.modifierFlags.contains(.option) {
                src.elevation = max(-60, min(75, src.elevation + Double(p.y - lastPoint.y) * 0.5))
            } else if let hit = groundPoint(p, height: SceneController.position(azimuth: src.azimuth, elevation: src.elevation, distance: src.distance).y) {
                let horiz = Double(hypot(hit.x, hit.z))
                let cosEl = cos(src.elevation * .pi / 180)
                src.azimuth = atan2(Double(hit.x), Double(-hit.z)) * 180 / .pi
                src.distance = max(0.5, min(4.0, horiz / max(cosEl, 0.2)))
            }
            controller.onSourceMoved?(i, src.azimuth, src.elevation, src.distance)
        case let .speaker(i):
            guard let hit = groundPoint(p, height: 0) else { return }
            let az = abs(atan2(Double(hit.x), Double(-hit.z)) * 180 / .pi)
            _ = i
            controller.onSpeakerSpanChanged?(max(10, min(120, az * 2)))
        case .none:
            break
        }
    }

    override func mouseUp(with event: NSEvent) {
        drag = .none
        controller?.draggingIndex = nil
    }

    override func scrollWheel(with event: NSEvent) {
        zoom(by: 1 + event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 0.004 : 0.04))
    }

    override func magnify(with event: NSEvent) {
        zoom(by: 1 - event.magnification)
    }

    private func zoom(by factor: CGFloat) {
        guard let cam = controller?.cameraNode else { return }
        cam.position.z = max(3.5, min(16, cam.position.z * factor))
    }

    /// Intersects the mouse ray with the horizontal plane y = height.
    private func groundPoint(_ p: NSPoint, height: CGFloat) -> SCNVector3? {
        let near = unprojectPoint(SCNVector3(p.x, p.y, 0))
        let far = unprojectPoint(SCNVector3(p.x, p.y, 1))
        let dy = far.y - near.y
        guard abs(dy) > 1e-6 else { return nil }
        let t = (height - near.y) / dy
        guard t > 0 else { return nil }
        return SCNVector3(near.x + (far.x - near.x) * t, height, near.z + (far.z - near.z) * t)
    }
}
