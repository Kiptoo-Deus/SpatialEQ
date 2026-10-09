import AudioToolbox
import CoreAudio
import Foundation

/// System-wide capture through a Core Audio process tap (macOS 14.2+), processed by the DSP engine
/// and played to a chosen output device.
///
///   all processes except us --(muted global tap)--> private aggregate device --IOProc--> DSP --> output device
///
/// The tap mutes the original audio, so the user hears only the processed signal. Our own process is
/// excluded from the tap so the processed output is not captured again.
final class AudioRouter {
    enum State: Equatable {
        case stopped
        case running(deviceName: String, sampleRate: Double)
        case failed(String)
    }

    private(set) var state: State = .stopped
    var onStateChange: ((State) -> Void)?
    /// Called (main thread) when the running route has to be rebuilt, e.g. after a sample-rate change.
    var onNeedsRestart: (() -> Void)?

    private let dsp: DSPEngine

    private var tapID = AudioObjectID(kAudioObjectUnknown)
    private var aggregateID = AudioObjectID(kAudioObjectUnknown)
    private var procID: AudioDeviceIOProcID?
    private var preparedRate: Double = 0
    private var watchers: [PropertyListener] = []

    init(dsp: DSPEngine) {
        self.dsp = dsp
    }

    deinit {
        teardown()
    }

    func start(output: OutputDevice) {
        stop()
        do {
            try build(output: output)
            set(.running(deviceName: output.name, sampleRate: currentSampleRate()))
        } catch {
            teardown()
            set(.failed(error.localizedDescription))
        }
    }

    func stop() {
        teardown()
        set(.stopped)
    }

    // MARK: - Route construction

    private func build(output: OutputDevice) throws {
        guard let ownProcess = CA.processObject(forPID: ProcessInfo.processInfo.processIdentifier) else {
            throw CoreAudioError(status: -1, what: "Looking up this app's audio process")
        }

        let description = CATapDescription(stereoGlobalTapButExcludeProcesses: [ownProcess])
        description.uuid = UUID()
        description.name = "SpatialEQ Tap"
        description.isPrivate = true
        description.muteBehavior = .muted

        var tap = AudioObjectID(kAudioObjectUnknown)
        try caCheck(AudioHardwareCreateProcessTap(description, &tap), "Creating the system audio tap")
        tapID = tap

        let aggregateUID = "\(DeviceManager.ownAggregatePrefix).\(UUID().uuidString)"
        let composition: [String: Any] = [
            kAudioAggregateDeviceNameKey: "SpatialEQ Engine",
            kAudioAggregateDeviceUIDKey: aggregateUID,
            kAudioAggregateDeviceMainSubDeviceKey: output.uid,
            kAudioAggregateDeviceIsPrivateKey: true,
            kAudioAggregateDeviceIsStackedKey: false,
            kAudioAggregateDeviceTapAutoStartKey: true,
            kAudioAggregateDeviceSubDeviceListKey: [[kAudioSubDeviceUIDKey: output.uid]],
            kAudioAggregateDeviceTapListKey: [[
                kAudioSubTapDriftCompensationKey: true,
                kAudioSubTapUIDKey: description.uuid.uuidString,
            ]],
        ]
        var aggregate = AudioObjectID(kAudioObjectUnknown)
        try caCheck(AudioHardwareCreateAggregateDevice(composition as CFDictionary, &aggregate),
                    "Creating the processing device")
        aggregateID = aggregate

        // The aggregate's input = the output device's own inputs (if any) followed by the tap.
        let skipChannels = Int32(CA.channelCount(output.id, scope: kAudioObjectPropertyScopeInput))

        preparedRate = currentSampleRate()
        dsp.prepare(sampleRate: preparedRate, maxFrames: 4096)

        let engine = dsp.handle
        var proc: AudioDeviceIOProcID?
        try caCheck(AudioDeviceCreateIOProcIDWithBlock(&proc, aggregate, nil) { _, inInput, _, outOutput, _ in
            sq_engine_process_abl(engine, inInput, skipChannels, outOutput)
        }, "Creating the audio callback")
        procID = proc

        try caCheck(AudioDeviceStart(aggregate, proc), "Starting audio")

        // Rebuild when the hardware changes under us.
        // Rebuild only on real changes: these properties also notify when nothing actually changed.
        let rateChanged: () -> Void = { [weak self] in
            guard let self, abs(self.currentSampleRate() - self.preparedRate) > 0.5 else { return }
            self.onNeedsRestart?()
        }
        let outputID = output.id
        let deviceDied: () -> Void = { [weak self] in
            guard CA.value(outputID, CA.address(kAudioDevicePropertyDeviceIsAlive), default: UInt32(0)) == 0 else { return }
            self?.onNeedsRestart?()
        }
        watchers = [
            PropertyListener(object: aggregate, address: CA.address(kAudioDevicePropertyNominalSampleRate), handler: rateChanged),
            PropertyListener(object: output.id, address: CA.address(kAudioDevicePropertyNominalSampleRate), handler: rateChanged),
            PropertyListener(object: output.id, address: CA.address(kAudioDevicePropertyDeviceIsAlive), handler: deviceDied),
        ].compactMap { $0 }
    }

    private func currentSampleRate() -> Double {
        let rate = CA.value(aggregateID, CA.address(kAudioDevicePropertyNominalSampleRate), default: Float64(0))
        return rate > 0 ? rate : 48000
    }

    private func teardown() {
        watchers.removeAll()
        if aggregateID != kAudioObjectUnknown {
            if let procID {
                AudioDeviceStop(aggregateID, procID)
                AudioDeviceDestroyIOProcID(aggregateID, procID)
            }
            AudioHardwareDestroyAggregateDevice(aggregateID)
        }
        if tapID != kAudioObjectUnknown {
            AudioHardwareDestroyProcessTap(tapID)
        }
        procID = nil
        aggregateID = AudioObjectID(kAudioObjectUnknown)
        tapID = AudioObjectID(kAudioObjectUnknown)
    }

    private func set(_ s: State) {
        state = s
        onStateChange?(s)
    }
}
