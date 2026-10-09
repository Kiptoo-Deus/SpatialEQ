import SceneKit
import os
#if os(macOS)
import AppKit
typealias PlatformColor = NSColor
typealias PlatformFont = NSFont
#else
import UIKit
typealias PlatformColor = UIColor
typealias PlatformFont = UIFont
#endif

/// Colours used by the 3D scene, shared by every Apple platform.
enum SceneTheme {
    static let background = PlatformColor(red: 0.035, green: 0.04, blue: 0.06, alpha: 1)
    static let accent = PlatformColor(red: 0.36, green: 0.85, blue: 0.95, alpha: 1)

    /// One colour per virtual channel: L R C Ls Rs Lb Rb.
    static let channelColors: [PlatformColor] = [
        PlatformColor(red: 0.30, green: 0.70, blue: 1.00, alpha: 1),
        PlatformColor(red: 1.00, green: 0.40, blue: 0.45, alpha: 1),
        PlatformColor(red: 0.95, green: 0.95, blue: 0.95, alpha: 1),
        PlatformColor(red: 0.55, green: 0.45, blue: 1.00, alpha: 1),
        PlatformColor(red: 1.00, green: 0.55, blue: 0.85, alpha: 1),
        PlatformColor(red: 0.35, green: 0.95, blue: 0.65, alpha: 1),
        PlatformColor(red: 1.00, green: 0.80, blue: 0.30, alpha: 1),
        PlatformColor(red: 0.70, green: 0.70, blue: 0.70, alpha: 1),
    ]

    static func rgb(_ c: PlatformColor) -> (Float, Float, Float) {
        var r: CGFloat = 0, g: CGFloat = 0, b: CGFloat = 0, a: CGFloat = 0
        #if os(macOS)
        (c.usingColorSpace(.deviceRGB) ?? c).getRed(&r, green: &g, blue: &b, alpha: &a)
        #else
        c.getRed(&r, green: &g, blue: &b, alpha: &a)
        #endif
        return (Float(r), Float(g), Float(b))
    }
}

/// Builds and animates the 3D scene: listener head, virtual sources, spectrum terrain and EQ ribbon.
/// Platform views (macOS / iOS) own the SCNView and translate input into source moves.
final class SceneController: NSObject, SCNSceneRendererDelegate {
    let scene = SCNScene()
    let cameraNode = SCNNode()
    let rig = SCNNode()

    var onSourceMoved: ((Int, Double, Double, Double) -> Void)?
    var onSpeakerSpanChanged: ((Double) -> Void)?

    private let analyzer: Analyzer
    private let head = SCNNode()
    private var sourceNodes: [SCNNode] = []
    private var speakerNodes: [SCNNode] = []
    private let rays = SCNNode()
    private let terrain = SCNNode()
    private let terrainLines = SCNNode()
    private let ribbon = SCNNode()
    private let terrainElement: SCNGeometryElement
    private let terrainMaterials: (fill: SCNMaterial, lines: SCNMaterial)

    // Shared between the main thread and the render thread.
    private var lock = os_unfair_lock()
    private var pending: (settings: SoundSettings, enabled: Bool, yaw: Double)?
    private var current = SoundSettings()
    private var enabled = true
    private var yaw = 0.0
    private var settingsVersion = 0
    private var renderedVersion = -1
    var draggingIndex: Int?

    static let floorY: Float = -1.25
    static let terrainWidth: Float = 7
    static let terrainDepth: Float = 6.5
    static let terrainFrontZ: Float = 2.2

    init(analyzer: Analyzer) {
        self.analyzer = analyzer
        terrainElement = Self.makeGridElement(cols: Analyzer.bandCount, rows: Analyzer.historyLength)

        let fill = SCNMaterial()
        fill.lightingModel = .constant
        fill.isDoubleSided = true
        fill.transparency = 0.55
        fill.writesToDepthBuffer = false
        let lines = SCNMaterial()
        lines.lightingModel = .constant
        lines.fillMode = .lines
        lines.diffuse.contents = PlatformColor(white: 1, alpha: 0.12)
        terrainMaterials = (fill, lines)
        super.init()
        buildScene()
    }

    func update(settings: SoundSettings, enabled: Bool, headYaw: Double) {
        os_unfair_lock_lock(&lock)
        pending = (settings, enabled, headYaw)
        os_unfair_lock_unlock(&lock)
    }

    /// Settings as last seen by the renderer (used by drag handling on the main thread).
    func currentSettings() -> SoundSettings {
        os_unfair_lock_lock(&lock)
        defer { os_unfair_lock_unlock(&lock) }
        return pending?.settings ?? current
    }

    // MARK: Scene construction

    private func buildScene() {
        scene.background.contents = SceneTheme.background
        scene.fogStartDistance = 9
        scene.fogEndDistance = 18
        scene.fogColor = SceneTheme.background

        let camera = SCNCamera()
        camera.fieldOfView = 45
        camera.zNear = 0.05
        camera.zFar = 60
        camera.wantsHDR = true
        camera.bloomIntensity = 1.4
        camera.bloomThreshold = 0.55
        camera.bloomBlurRadius = 10
        camera.vignettingIntensity = 0.4
        cameraNode.camera = camera
        cameraNode.position = SCNVector3(0, 0, 8)
        rig.addChildNode(cameraNode)
        resetCamera()
        scene.rootNode.addChildNode(rig)

        let ambient = SCNNode()
        ambient.light = SCNLight()
        ambient.light!.type = .ambient
        ambient.light!.intensity = 250
        scene.rootNode.addChildNode(ambient)

        let key = SCNNode()
        key.light = SCNLight()
        key.light!.type = .directional
        key.light!.intensity = 900
        key.eulerAngles = SCNVector3(-0.8, 0.6, 0)
        scene.rootNode.addChildNode(key)

        let rim = SCNNode()
        rim.light = SCNLight()
        rim.light!.type = .omni
        rim.light!.color = SceneTheme.accent
        rim.light!.intensity = 600
        rim.position = SCNVector3(0, 1.5, -3)
        scene.rootNode.addChildNode(rim)

        buildHead()
        buildRings()

        for i in 0..<SoundSettings.sourceCount {
            let node = makeSourceNode(index: i)
            sourceNodes.append(node)
            scene.rootNode.addChildNode(node)
        }
        for (i, name) in ["L", "R"].enumerated() {
            let node = makeSpeakerNode(index: i, label: name)
            speakerNodes.append(node)
            scene.rootNode.addChildNode(node)
        }

        terrain.renderingOrder = -1
        terrainLines.renderingOrder = 0
        scene.rootNode.addChildNode(terrain)
        scene.rootNode.addChildNode(terrainLines)
        scene.rootNode.addChildNode(ribbon)
        scene.rootNode.addChildNode(rays)
    }

    func resetCamera() {
        rig.eulerAngles = SCNVector3(-0.5, 0, 0)
        cameraNode.position = SCNVector3(0, 0, 8)
    }

    private func buildHead() {
        let skin = SCNMaterial()
        skin.lightingModel = .physicallyBased
        skin.diffuse.contents = PlatformColor(white: 0.32, alpha: 1)
        skin.metalness.contents = 0.25
        skin.roughness.contents = 0.35

        let skull = SCNNode(geometry: SCNSphere(radius: 0.36))
        skull.geometry!.materials = [skin]
        skull.scale = SCNVector3(0.85, 1.05, 1.0)
        head.addChildNode(skull)

        for side: CGFloat in [-1, 1] {
            let ear = SCNNode(geometry: SCNSphere(radius: 0.1))
            ear.geometry!.materials = [skin]
            ear.scale = SCNVector3(0.35, 1.0, 0.65)
            ear.position = SCNVector3(side * 0.31, 0, 0.02)
            head.addChildNode(ear)
        }
        let nose = SCNNode(geometry: SCNCone(topRadius: 0, bottomRadius: 0.06, height: 0.14))
        nose.geometry!.materials = [skin]
        nose.eulerAngles = SCNVector3(-CGFloat.pi / 2, 0, 0)
        nose.position = SCNVector3(0, -0.02, -0.38)
        head.addChildNode(nose)

        // Facing indicator on the floor.
        let arrow = SCNNode(geometry: SCNCone(topRadius: 0, bottomRadius: 0.08, height: 0.3))
        let glow = SCNMaterial()
        glow.lightingModel = .constant
        glow.diffuse.contents = SceneTheme.accent.withAlphaComponent(0.8)
        arrow.geometry!.materials = [glow]
        arrow.eulerAngles = SCNVector3(-CGFloat.pi / 2, 0, 0)
        arrow.position = SCNVector3(0, -0.55, -0.6)
        head.addChildNode(arrow)

        scene.rootNode.addChildNode(head)
    }

    private func buildRings() {
        let mat = SCNMaterial()
        mat.lightingModel = .constant
        mat.diffuse.contents = PlatformColor(white: 1, alpha: 0.12)
        for r in [1.0, 2.0, 3.0] {
            let ring = SCNNode(geometry: SCNTorus(ringRadius: CGFloat(r), pipeRadius: 0.006))
            ring.geometry!.materials = [mat]
            ring.position = SCNVector3(0, -0.55, 0)
            scene.rootNode.addChildNode(ring)
        }
    }

    private func makeSourceNode(index: Int) -> SCNNode {
        let color = SceneTheme.channelColors[index]
        let node = SCNNode()
        node.name = "source-\(index)"

        let orb = SCNNode(geometry: SCNSphere(radius: 0.13))
        let m = SCNMaterial()
        m.lightingModel = .constant
        m.diffuse.contents = color
        m.emission.contents = color
        orb.geometry!.materials = [m]
        orb.name = "orb"
        node.addChildNode(orb)

        let halo = SCNNode(geometry: SCNSphere(radius: 0.22))
        let hm = SCNMaterial()
        hm.lightingModel = .constant
        hm.diffuse.contents = color.withAlphaComponent(0.15)
        hm.writesToDepthBuffer = false
        halo.geometry!.materials = [hm]
        halo.name = "halo"
        node.addChildNode(halo)

        let text = SCNText(string: "", extrusionDepth: 0)
        text.font = PlatformFont.systemFont(ofSize: 1, weight: .semibold)
        text.flatness = 0.05
        let tm = SCNMaterial()
        tm.lightingModel = .constant
        tm.diffuse.contents = PlatformColor.white
        text.materials = [tm]
        let label = SCNNode(geometry: text)
        label.name = "label"
        label.scale = SCNVector3(0.22, 0.22, 0.22)
        label.position = SCNVector3(-0.08, 0.2, 0)
        label.constraints = [SCNBillboardConstraint()]
        node.addChildNode(label)
        return node
    }

    private func makeSpeakerNode(index: Int, label: String) -> SCNNode {
        let node = SCNNode()
        node.name = "speaker-\(index)"
        let box = SCNNode(geometry: SCNBox(width: 0.38, height: 0.6, length: 0.32, chamferRadius: 0.04))
        let m = SCNMaterial()
        m.lightingModel = .physicallyBased
        m.diffuse.contents = PlatformColor(white: 0.18, alpha: 1)
        m.roughness.contents = 0.5
        box.geometry!.materials = [m]
        node.addChildNode(box)

        let cone = SCNNode(geometry: SCNCylinder(radius: 0.11, height: 0.02))
        let cm = SCNMaterial()
        cm.lightingModel = .constant
        cm.emission.contents = SceneTheme.channelColors[index]
        cm.diffuse.contents = SceneTheme.channelColors[index]
        cone.geometry!.materials = [cm]
        cone.name = "cone"
        cone.eulerAngles = SCNVector3(CGFloat.pi / 2, 0, 0)
        cone.position = SCNVector3(0, -0.08, 0.17)
        node.addChildNode(cone)
        return node
    }

    // MARK: Per-frame update (render thread)

    func renderer(_ renderer: SCNSceneRenderer, updateAtTime time: TimeInterval) {
        os_unfair_lock_lock(&lock)
        if let p = pending {
            if p.settings != current { settingsVersion += 1 }
            current = p.settings
            enabled = p.enabled
            yaw = p.yaw
            pending = nil
        }
        let s = current
        let version = settingsVersion
        let yawNow = yaw
        os_unfair_lock_unlock(&lock)

        let snap = analyzer.snapshot()
        let pulse = CGFloat(1 + snap.level * 0.6)

        head.eulerAngles = SCNVector3(0, CGFloat(-yawNow * .pi / 180), 0)

        let headphones = s.mode == .headphones
        let spatial = headphones && s.spatialEnabled
        let activeSources = spatial ? s.upmix.channelNames.count : 0
        for (i, node) in sourceNodes.enumerated() {
            let active = i < activeSources
            node.isHidden = !active
            guard active else { continue }
            let src = s.sources[i]
            node.position = Self.position(azimuth: src.azimuth, elevation: src.elevation, distance: src.distance)
            if let halo = node.childNode(withName: "halo", recursively: false) {
                let k = i == draggingIndex ? pulse * 1.4 : pulse
                halo.scale = SCNVector3(k, k, k)
            }
            if let label = node.childNode(withName: "label", recursively: false),
               let text = label.geometry as? SCNText {
                let name = s.upmix.channelNames[i]
                if (text.string as? String) != name { text.string = name }
            }
        }

        // Speaker mode (or plain stereo headphones) shows two speakers.
        for (i, node) in speakerNodes.enumerated() {
            node.isHidden = spatial
            guard !spatial else { continue }
            let half = (headphones ? 30 : s.speakerSpan / 2) * (i == 0 ? -1 : 1)
            node.position = Self.position(azimuth: half, elevation: 0, distance: 2.2)
            node.eulerAngles = SCNVector3(0, CGFloat(-half * .pi / 180), 0)
            if let cone = node.childNode(withName: "cone", recursively: false) {
                cone.scale = SCNVector3(pulse, 1, pulse)
            }
        }

        updateRays(settings: s, sources: activeSources)
        updateTerrain(snap)
        if version != renderedVersion {
            renderedVersion = version
            updateRibbon(settings: s)
        }
        terrain.opacity = enabled ? 1 : 0.35
    }

    static func position(azimuth: Double, elevation: Double, distance: Double) -> SCNVector3 {
        let az = azimuth * .pi / 180, el = elevation * .pi / 180
        return SCNVector3(CGFloat(distance * sin(az) * cos(el)),
                          CGFloat(distance * sin(el)),
                          CGFloat(-distance * cos(az) * cos(el)))
    }

    private func updateRays(settings s: SoundSettings, sources: Int) {
        guard sources > 0 else { rays.geometry = nil; return }
        var verts: [SCNVector3] = []
        var colors: [Float] = []
        for i in 0..<sources {
            let p = Self.position(azimuth: s.sources[i].azimuth, elevation: s.sources[i].elevation, distance: s.sources[i].distance)
            verts.append(SCNVector3(0, 0, 0))
            verts.append(p)
            let c = SceneTheme.rgb(SceneTheme.channelColors[i])
            for alpha: Float in [0.0, 0.5] {
                colors += [c.0, c.1, c.2, alpha]
            }
        }
        let indices = (0..<Int32(verts.count)).map { $0 }
        let geo = SCNGeometry(sources: [SCNGeometrySource(vertices: verts), Self.colorSource(colors, count: verts.count)],
                              elements: [SCNGeometryElement(indices: indices, primitiveType: .line)])
        let m = SCNMaterial()
        m.lightingModel = .constant
        m.blendMode = .add
        geo.materials = [m]
        rays.geometry = geo
    }

    private func updateTerrain(_ snap: Analyzer.Snapshot) {
        let cols = Analyzer.bandCount, rows = Analyzer.historyLength
        var verts = [SCNVector3]()
        verts.reserveCapacity(cols * rows)
        var colors = [Float]()
        colors.reserveCapacity(cols * rows * 4)
        for r in 0..<rows {
            let row = r < snap.history.count ? snap.history[r] : []
            let z = Self.terrainFrontZ - Float(r) / Float(rows - 1) * Self.terrainDepth
            let fade = 1 - Float(r) / Float(rows)
            for c in 0..<cols {
                let v = c < row.count ? row[c] : 0
                let x = (Float(c) / Float(cols - 1) - 0.5) * Self.terrainWidth
                verts.append(SCNVector3(CGFloat(x), CGFloat(Self.floorY + v * 1.3), CGFloat(z)))
                let (cr, cg, cb) = Self.heatColor(v)
                colors += [cr * fade, cg * fade, cb * fade, 1]
            }
        }
        let geo = SCNGeometry(sources: [SCNGeometrySource(vertices: verts), Self.colorSource(colors, count: verts.count)],
                              elements: [terrainElement])
        geo.materials = [terrainMaterials.fill]
        terrain.geometry = geo
        let lineGeo = SCNGeometry(sources: [SCNGeometrySource(vertices: verts)], elements: [terrainElement])
        lineGeo.materials = [terrainMaterials.lines]
        terrainLines.geometry = lineGeo
    }

    /// EQ curve drawn as a glowing ribbon floating over the front edge of the terrain.
    private func updateRibbon(settings s: SoundSettings) {
        let n = 160
        let freqs: [Float] = (0..<n).map {
            Analyzer.minFreq * pow(Analyzer.maxFreq / Analyzer.minFreq, Float($0) / Float(n - 1))
        }
        let db = DSPEngine.eqResponse(s, sampleRate: 48000, frequencies: freqs)
        var verts: [SCNVector3] = []
        var colors: [Float] = []
        let z = CGFloat(Self.terrainFrontZ + 0.15)
        for i in 0..<n {
            let x = CGFloat((Float(i) / Float(n - 1) - 0.5) * Self.terrainWidth)
            let y = CGFloat(Self.floorY + 0.9 + max(-18, min(18, db[i])) / 18 * 0.8)
            verts.append(SCNVector3(x, y - 0.025, z))
            verts.append(SCNVector3(x, y + 0.025, z))
            colors += [1, 0.62, 0.27, 1, 1, 0.75, 0.4, 1]
        }
        var idx: [Int32] = []
        for i in 0..<(n - 1) {
            let a = Int32(i * 2)
            idx += [a, a + 1, a + 2, a + 1, a + 3, a + 2]
        }
        let geo = SCNGeometry(sources: [SCNGeometrySource(vertices: verts), Self.colorSource(colors, count: verts.count)],
                              elements: [SCNGeometryElement(indices: idx, primitiveType: .triangles)])
        let m = SCNMaterial()
        m.lightingModel = .constant
        m.isDoubleSided = true
        geo.materials = [m]
        ribbon.geometry = geo
    }

    private static func heatColor(_ v: Float) -> (Float, Float, Float) {
        // deep blue -> cyan -> orange -> white
        let stops: [(Float, (Float, Float, Float))] = [
            (0.0, (0.03, 0.05, 0.15)), (0.35, (0.10, 0.35, 0.75)), (0.6, (0.30, 0.85, 0.95)),
            (0.8, (0.98, 0.60, 0.25)), (1.0, (1.0, 0.95, 0.85)),
        ]
        for i in 1..<stops.count where v <= stops[i].0 {
            let (t0, c0) = stops[i - 1], (t1, c1) = stops[i]
            let t = (v - t0) / (t1 - t0)
            return (c0.0 + (c1.0 - c0.0) * t, c0.1 + (c1.1 - c0.1) * t, c0.2 + (c1.2 - c0.2) * t)
        }
        return stops.last!.1
    }

    private static func colorSource(_ rgba: [Float], count: Int) -> SCNGeometrySource {
        let data = rgba.withUnsafeBufferPointer { Data(buffer: $0) }
        return SCNGeometrySource(data: data, semantic: .color, vectorCount: count, usesFloatComponents: true,
                                 componentsPerVector: 4, bytesPerComponent: 4, dataOffset: 0, dataStride: 16)
    }

    private static func makeGridElement(cols: Int, rows: Int) -> SCNGeometryElement {
        var idx: [Int32] = []
        idx.reserveCapacity((cols - 1) * (rows - 1) * 6)
        for r in 0..<(rows - 1) {
            for c in 0..<(cols - 1) {
                let a = Int32(r * cols + c), b = a + 1, d = a + Int32(cols), e = d + 1
                idx += [a, d, b, b, d, e]
            }
        }
        return SCNGeometryElement(indices: idx, primitiveType: .triangles)
    }
}

