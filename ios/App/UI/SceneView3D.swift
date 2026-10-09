import SceneKit
import SwiftUI

/// Touch version of the 3D scene. Drag a source to move it, drag elsewhere to orbit,
/// pinch to zoom, double-tap to reset the camera.
struct SceneView3D: UIViewRepresentable {
    @EnvironmentObject var state: PlayerState
    @EnvironmentObject var tracker: HeadTracker

    func makeCoordinator() -> Coordinator {
        Coordinator(controller: SceneController(analyzer: state.analyzer))
    }

    func makeUIView(context: Context) -> SCNView {
        let view = SCNView(frame: .zero)
        let c = context.coordinator
        view.scene = c.controller.scene
        view.pointOfView = c.controller.cameraNode
        view.delegate = c.controller
        view.backgroundColor = SceneTheme.background
        view.antialiasingMode = .multisampling2X
        view.rendersContinuously = true
        view.preferredFramesPerSecond = 60
        c.view = view
        c.controller.onSourceMoved = { [weak state] i, az, el, dist in
            DispatchQueue.main.async { state?.setSource(i, azimuth: az, elevation: el, distance: dist) }
        }
        c.controller.onSpeakerSpanChanged = { [weak state] span in
            DispatchQueue.main.async { state?.settings.speakerSpan = span }
        }

        let pan = UIPanGestureRecognizer(target: c, action: #selector(Coordinator.pan(_:)))
        pan.maximumNumberOfTouches = 1
        view.addGestureRecognizer(pan)
        view.addGestureRecognizer(UIPinchGestureRecognizer(target: c, action: #selector(Coordinator.pinch(_:))))
        let doubleTap = UITapGestureRecognizer(target: c, action: #selector(Coordinator.doubleTap))
        doubleTap.numberOfTapsRequired = 2
        view.addGestureRecognizer(doubleTap)
        return view
    }

    func updateUIView(_ view: SCNView, context: Context) {
        context.coordinator.controller.update(settings: state.settings, enabled: state.enabled, headYaw: tracker.yawDegrees)
    }

    final class Coordinator: NSObject {
        let controller: SceneController
        weak var view: SCNView?
        private enum Drag { case none, orbit, source(Int), speaker }
        private var drag: Drag = .none
        private var last: CGPoint = .zero
        private var pinchStart: Float = 8

        init(controller: SceneController) {
            self.controller = controller
        }

        @objc func pan(_ g: UIPanGestureRecognizer) {
            guard let view else { return }
            let p = g.location(in: view)
            switch g.state {
            case .began:
                last = p
                drag = .orbit
                let hits = view.hitTest(p, options: [.searchMode: SCNHitTestSearchMode.all.rawValue, .ignoreHiddenNodes: true])
                outer: for hit in hits {
                    var node: SCNNode? = hit.node
                    while let n = node {
                        if let name = n.name, name.hasPrefix("source-"), let i = Int(name.dropFirst(7)) {
                            drag = .source(i)
                            controller.draggingIndex = i
                            UIImpactFeedbackGenerator(style: .light).impactOccurred()
                            break outer
                        }
                        if let name = n.name, name.hasPrefix("speaker-") { drag = .speaker; break outer }
                        node = n.parent
                    }
                }
            case .changed:
                move(to: p, in: view)
                last = p
            default:
                drag = .none
                controller.draggingIndex = nil
            }
        }

        private func move(to p: CGPoint, in view: SCNView) {
            switch drag {
            case .orbit:
                var e = controller.rig.eulerAngles
                e.y -= Float(p.x - last.x) * 0.008
                e.x = max(-1.45, min(0.2, e.x - Float(p.y - last.y) * 0.008))
                controller.rig.eulerAngles = e
            case let .source(i):
                let s = controller.currentSettings()
                guard s.sources.indices.contains(i) else { return }
                var src = s.sources[i]
                let y = SceneController.position(azimuth: src.azimuth, elevation: src.elevation, distance: src.distance).y
                guard let hit = groundPoint(p, height: y, in: view) else { return }
                let cosEl = cos(src.elevation * .pi / 180)
                src.azimuth = atan2(Double(hit.x), Double(-hit.z)) * 180 / .pi
                src.distance = max(0.5, min(4, Double(hypot(hit.x, hit.z)) / max(cosEl, 0.2)))
                controller.onSourceMoved?(i, src.azimuth, src.elevation, src.distance)
            case .speaker:
                guard let hit = groundPoint(p, height: 0, in: view) else { return }
                let az = abs(atan2(Double(hit.x), Double(-hit.z)) * 180 / .pi)
                controller.onSpeakerSpanChanged?(max(10, min(120, az * 2)))
            case .none:
                break
            }
        }

        @objc func pinch(_ g: UIPinchGestureRecognizer) {
            if g.state == .began { pinchStart = controller.cameraNode.position.z }
            controller.cameraNode.position.z = max(3.5, min(16, pinchStart / Float(max(g.scale, 0.01))))
        }

        @objc func doubleTap() {
            controller.resetCamera()
        }

        private func groundPoint(_ p: CGPoint, height: Float, in view: SCNView) -> SCNVector3? {
            let near = view.unprojectPoint(SCNVector3(Float(p.x), Float(p.y), 0))
            let far = view.unprojectPoint(SCNVector3(Float(p.x), Float(p.y), 1))
            let dy = far.y - near.y
            guard abs(dy) > 1e-6 else { return nil }
            let t = (height - near.y) / dy
            guard t > 0 else { return nil }
            return SCNVector3(near.x + (far.x - near.x) * t, height, near.z + (far.z - near.z) * t)
        }
    }
}
