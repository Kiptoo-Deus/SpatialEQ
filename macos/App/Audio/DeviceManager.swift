import CoreAudio
import Foundation

struct OutputDevice: Identifiable, Hashable {
    let id: AudioObjectID
    let uid: String
    let name: String
    let transport: UInt32
    let sampleRate: Double
    let latencyFrames: UInt32
    let isHeadphoneLike: Bool

    var isBluetooth: Bool {
        transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE
    }

    var isAirPodsLike: Bool {
        isBluetooth && (name.localizedCaseInsensitiveContains("airpods") || name.localizedCaseInsensitiveContains("beats"))
    }

    var latencyMs: Double { sampleRate > 0 ? Double(latencyFrames) / sampleRate * 1000 : 0 }

    var symbolName: String {
        if isHeadphoneLike { return isAirPodsLike ? "airpodspro" : "headphones" }
        if transport == kAudioDeviceTransportTypeBuiltIn { return "laptopcomputer" }
        if transport == kAudioDeviceTransportTypeHDMI || transport == kAudioDeviceTransportTypeDisplayPort { return "tv" }
        return "hifispeaker"
    }
}

/// Tracks the system's output devices and the default output, publishing changes on the main thread.
final class DeviceManager: ObservableObject {
    static let ownAggregatePrefix = "com.spatialeq.aggregate"

    @Published private(set) var devices: [OutputDevice] = []
    @Published private(set) var defaultOutputUID: String?

    /// Fired when the device list or the default device changes.
    var onChange: (() -> Void)?

    private var listeners: [PropertyListener] = []
    private var dataSourceListeners: [PropertyListener] = []

    init() {
        refresh()
        let system = AudioObjectID(kAudioObjectSystemObject)
        for selector in [kAudioHardwarePropertyDevices, kAudioHardwarePropertyDefaultOutputDevice] {
            if let l = PropertyListener(object: system, address: CA.address(selector), handler: { [weak self] in
                self?.refresh()
                self?.onChange?()
            }) {
                listeners.append(l)
            }
        }
    }

    func device(uid: String?) -> OutputDevice? {
        guard let uid else { return nil }
        return devices.first { $0.uid == uid }
    }

    func refresh() {
        let system = AudioObjectID(kAudioObjectSystemObject)
        let ids = CA.array(system, CA.address(kAudioHardwarePropertyDevices), of: AudioObjectID.self)
        devices = ids.compactMap(Self.describe).sorted { $0.name.localizedCompare($1.name) == .orderedAscending }

        let defaultID = CA.value(system, CA.address(kAudioHardwarePropertyDefaultOutputDevice),
                                 default: AudioObjectID(kAudioObjectUnknown))
        defaultOutputUID = CA.string(defaultID, CA.address(kAudioDevicePropertyDeviceUID))

        // Built-in outputs switch between speakers and headphones via the data source.
        dataSourceListeners = devices.filter { $0.transport == kAudioDeviceTransportTypeBuiltIn }.compactMap { dev in
            PropertyListener(object: dev.id, address: CA.address(kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeOutput)) { [weak self] in
                self?.refresh()
                self?.onChange?()
            }
        }
    }

    private static func describe(_ id: AudioObjectID) -> OutputDevice? {
        guard CA.channelCount(id, scope: kAudioObjectPropertyScopeOutput) > 0,
              let uid = CA.string(id, CA.address(kAudioDevicePropertyDeviceUID)),
              !uid.hasPrefix(ownAggregatePrefix) else { return nil }
        let name = CA.string(id, CA.address(kAudioObjectPropertyName)) ?? "Unknown Device"
        let transport = CA.value(id, CA.address(kAudioDevicePropertyTransportType), default: UInt32(0))
        let rate = CA.value(id, CA.address(kAudioDevicePropertyNominalSampleRate), default: Float64(0))
        let latency = CA.value(id, CA.address(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput), default: UInt32(0))
            + CA.value(id, CA.address(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput), default: UInt32(0))

        var headphoneLike = transport == kAudioDeviceTransportTypeBluetooth || transport == kAudioDeviceTransportTypeBluetoothLE
        let dsAddr = CA.address(kAudioDevicePropertyDataSource, kAudioObjectPropertyScopeOutput)
        if transport == kAudioDeviceTransportTypeBuiltIn, CA.has(id, dsAddr) {
            headphoneLike = CA.value(id, dsAddr, default: UInt32(0)) == CA.fourCC("hdpn")
        }
        if name.localizedCaseInsensitiveContains("headphone") || name.localizedCaseInsensitiveContains("airpods") {
            headphoneLike = true
        }
        return OutputDevice(id: id, uid: uid, name: name, transport: transport, sampleRate: rate,
                            latencyFrames: latency, isHeadphoneLike: headphoneLike)
    }
}
