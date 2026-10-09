import CoreMotion
import Foundation

/// AirPods / Beats head tracking through CMHeadphoneMotionManager (macOS 14+).
final class HeadTracker: NSObject, ObservableObject, CMHeadphoneMotionManagerDelegate {
    @Published private(set) var isConnected = false
    @Published private(set) var isRunning = false
    @Published private(set) var yawDegrees: Double = 0
    @Published private(set) var errorMessage: String?

    /// Yaw in degrees, positive = head turned right, relative to the last recentre.
    var onYaw: ((Double) -> Void)?

    private let manager = CMHeadphoneMotionManager()
    private var reference: Double = 0
    private var rawYaw: Double = 0

    var isSupported: Bool { manager.isDeviceMotionAvailable }

    override init() {
        super.init()
        manager.delegate = self
    }

    func start() {
        guard isSupported else {
            errorMessage = "Head tracking needs AirPods (3rd gen / Pro / Max) or supported Beats."
            return
        }
        guard !isRunning else { return }
        errorMessage = nil
        isRunning = true
        manager.startDeviceMotionUpdates(to: .main) { [weak self] motion, error in
            guard let self else { return }
            if let error {
                self.errorMessage = error.localizedDescription
                return
            }
            guard let motion else { return }
            // CoreMotion yaw is counter-clockwise positive (turning left); we use right-positive.
            self.rawYaw = -motion.attitude.yaw * 180 / .pi
            let y = Self.wrap(self.rawYaw - self.reference)
            self.yawDegrees = y
            self.onYaw?(y)
        }
    }

    func stop() {
        manager.stopDeviceMotionUpdates()
        isRunning = false
        yawDegrees = 0
        onYaw?(0)
    }

    func recenter() {
        reference = rawYaw
    }

    func headphoneMotionManagerDidConnect(_ manager: CMHeadphoneMotionManager) {
        DispatchQueue.main.async { self.isConnected = true }
    }

    func headphoneMotionManagerDidDisconnect(_ manager: CMHeadphoneMotionManager) {
        DispatchQueue.main.async { self.isConnected = false }
    }

    private static func wrap(_ deg: Double) -> Double {
        var d = deg.truncatingRemainder(dividingBy: 360)
        if d > 180 { d -= 360 }
        if d < -180 { d += 360 }
        return d
    }
}
