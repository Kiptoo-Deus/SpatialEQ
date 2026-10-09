import CoreAudio
import Foundation

struct CoreAudioError: LocalizedError {
    let status: OSStatus
    let what: String

    var errorDescription: String? {
        "\(what) failed (\(CoreAudioError.fourCC(status)))"
    }

    static func fourCC(_ status: OSStatus) -> String {
        let n = UInt32(bitPattern: status)
        let bytes = [24, 16, 8, 0].map { UInt8((n >> $0) & 0xFF) }
        if bytes.allSatisfy({ $0 >= 32 && $0 < 127 }) {
            return "'" + String(decoding: bytes, as: UTF8.self) + "'"
        }
        return String(status)
    }
}

@discardableResult
func caCheck(_ status: OSStatus, _ what: @autoclosure () -> String) throws -> OSStatus {
    guard status == noErr else { throw CoreAudioError(status: status, what: what()) }
    return status
}

enum CA {
    static func address(_ selector: AudioObjectPropertySelector,
                        _ scope: AudioObjectPropertyScope = kAudioObjectPropertyScopeGlobal,
                        _ element: AudioObjectPropertyElement = kAudioObjectPropertyElementMain) -> AudioObjectPropertyAddress {
        AudioObjectPropertyAddress(mSelector: selector, mScope: scope, mElement: element)
    }

    static func has(_ object: AudioObjectID, _ addr: AudioObjectPropertyAddress) -> Bool {
        var a = addr
        return AudioObjectHasProperty(object, &a)
    }

    static func value<T>(_ object: AudioObjectID, _ addr: AudioObjectPropertyAddress, default def: T) -> T {
        var a = addr
        var v = def
        var size = UInt32(MemoryLayout<T>.size)
        let status = withUnsafeMutablePointer(to: &v) { AudioObjectGetPropertyData(object, &a, 0, nil, &size, $0) }
        return status == noErr ? v : def
    }

    static func array<T>(_ object: AudioObjectID, _ addr: AudioObjectPropertyAddress, of _: T.Type) -> [T] {
        var a = addr
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(object, &a, 0, nil, &size) == noErr, size > 0 else { return [] }
        let count = Int(size) / MemoryLayout<T>.stride
        let ptr = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<T>.alignment)
        defer { ptr.deallocate() }
        guard AudioObjectGetPropertyData(object, &a, 0, nil, &size, ptr) == noErr else { return [] }
        let typed = ptr.bindMemory(to: T.self, capacity: count)
        return Array(UnsafeBufferPointer(start: typed, count: Int(size) / MemoryLayout<T>.stride))
    }

    static func string(_ object: AudioObjectID, _ addr: AudioObjectPropertyAddress) -> String? {
        var a = addr
        var ref: Unmanaged<CFString>?
        var size = UInt32(MemoryLayout<Unmanaged<CFString>?>.size)
        let status = withUnsafeMutablePointer(to: &ref) { AudioObjectGetPropertyData(object, &a, 0, nil, &size, $0) }
        guard status == noErr, let ref else { return nil }
        return ref.takeRetainedValue() as String
    }

    /// Total channels across all stream configurations in a scope.
    static func channelCount(_ device: AudioObjectID, scope: AudioObjectPropertyScope) -> Int {
        var a = address(kAudioDevicePropertyStreamConfiguration, scope)
        var size: UInt32 = 0
        guard AudioObjectGetPropertyDataSize(device, &a, 0, nil, &size) == noErr, size > 0 else { return 0 }
        let raw = UnsafeMutableRawPointer.allocate(byteCount: Int(size), alignment: MemoryLayout<AudioBufferList>.alignment)
        defer { raw.deallocate() }
        guard AudioObjectGetPropertyData(device, &a, 0, nil, &size, raw) == noErr else { return 0 }
        let list = UnsafeMutableAudioBufferListPointer(raw.assumingMemoryBound(to: AudioBufferList.self))
        return list.reduce(0) { $0 + Int($1.mNumberChannels) }
    }

    static func device(forUID uid: String) -> AudioObjectID? {
        var a = address(kAudioHardwarePropertyTranslateUIDToDevice)
        var cfUID = uid as CFString
        var id = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        let status = withUnsafeMutablePointer(to: &cfUID) { uidPtr in
            AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a,
                                       UInt32(MemoryLayout<CFString>.size), uidPtr, &size, &id)
        }
        return status == noErr && id != kAudioObjectUnknown ? id : nil
    }

    static func processObject(forPID pid: pid_t) -> AudioObjectID? {
        var a = address(kAudioHardwarePropertyTranslatePIDToProcessObject)
        var pid = pid
        var id = AudioObjectID(kAudioObjectUnknown)
        var size = UInt32(MemoryLayout<AudioObjectID>.size)
        let status = AudioObjectGetPropertyData(AudioObjectID(kAudioObjectSystemObject), &a,
                                                UInt32(MemoryLayout<pid_t>.size), &pid, &size, &id)
        return status == noErr && id != kAudioObjectUnknown ? id : nil
    }

    static func fourCC(_ s: String) -> UInt32 {
        s.utf8.reduce(0) { ($0 << 8) | UInt32($1) }
    }
}

/// Keeps an AudioObject property listener alive and removes it on deinit.
final class PropertyListener {
    private let object: AudioObjectID
    private var addr: AudioObjectPropertyAddress
    private let block: AudioObjectPropertyListenerBlock
    private let queue: DispatchQueue

    init?(object: AudioObjectID, address: AudioObjectPropertyAddress, queue: DispatchQueue = .main,
          handler: @escaping () -> Void) {
        self.object = object
        self.addr = address
        self.queue = queue
        self.block = { _, _ in handler() }
        guard AudioObjectAddPropertyListenerBlock(object, &addr, queue, block) == noErr else { return nil }
    }

    deinit {
        AudioObjectRemovePropertyListenerBlock(object, &addr, queue, block)
    }
}
