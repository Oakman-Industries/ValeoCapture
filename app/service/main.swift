// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation
import Darwin

func bridgeValue(_ value: UnsafeMutablePointer<CChar>?) -> String {
    guard let value = value else { return "{\"schema\":1,\"ok\":false,\"error\":\"Allocation failed\"}" }
    defer { VCFree(value) }
    return String(cString: value)
}

final class CaptureService: NSObject, ValeoCaptureServiceProtocol {
    // Short operations own their transport within one call. Persistent capture
    // uses its own thread/CFRunLoop for the complete transport lifetime.
    private let worker = DispatchQueue(label: "org.valeocapture.capture-worker")
    // Retained XPC replies preserve the caller's importance boost until drain
    // and teardown complete; a detached Thread alone loses that request context.
    private let lifetimeLock = NSLock()
    private var lifetime: SessionLifetime?
    func waitForCaptureToFinish(_ reply: @escaping (String) -> Void) {
        lifetimeLock.lock(); let current = lifetime; lifetimeLock.unlock()
        if let current = current { current.wait(reply) } else { reply(bridgeValue(VCCaptureState())) }
    }
    func snapshot(_ reply: @escaping (String) -> Void) {
        worker.async { reply(bridgeValue(VCDiscover())) }
    }
    func probe(_ registryID: String, reply: @escaping (String) -> Void) {
        guard let id = UInt64(registryID), id != 0 else {
            reply("{\"schema\":1,\"ok\":false,\"error\":\"Invalid registry ID\"}"); return
        }
        worker.async { reply(bridgeValue(VCProbe(id))) }
    }
    func inspect(_ registryID: String, reply: @escaping (String) -> Void) {
        guard let id = UInt64(registryID), id != 0 else { reply("{\"schema\":1,\"ok\":false,\"error\":\"Invalid registry ID\"}"); return }
        worker.async { reply(bridgeValue(VCInspectCamera(id))) }
    }
    func captureState(_ reply: @escaping (String) -> Void) { reply(bridgeValue(VCCaptureState())) }
    func stopCapture(_ reply: @escaping (String) -> Void) { reply(bridgeValue(VCStopCapture())) }
    func requestSoftwareISO(_ iso: Int, reply: @escaping (String) -> Void) {
        guard [25, 50, 100, 200].contains(iso) else { reply("{\"schema\":1,\"ok\":false,\"error\":\"Invalid ISO\"}"); return }
        reply(bridgeValue(VCRequestSoftwareISO(UInt32(iso))))
    }
    func startCapture(_ registryID: String, folder: String, iso: Int, orientation: Int,
                      profile: Data, calibration: Data, saveTIFF: Bool, reply: @escaping (String) -> Void) {
        do {
            guard let id = UInt64(registryID), id != 0, [25, 50, 100, 200].contains(iso), [1, 3, 6, 8].contains(orientation),
                  profile.count >= 128, profile.count <= 16 * 1024 * 1024, folder.hasPrefix("/"), folder.utf8.count < 4096 else {
                throw NSError(domain: "ValeoCapture", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid capture settings"])
            }
            let metadata = try CameraCalibration(calibration)
            let reserved = bridgeValue(VCReserveCapture(UInt32(iso)))
            guard let data = reserved.data(using: .utf8), let value = try JSONSerialization.jsonObject(with: data) as? [String: Any], value["ok"] as? Bool == true else {
                reply(reserved); return
            }
            let current = SessionLifetime()
            lifetimeLock.lock(); lifetime = current; lifetimeLock.unlock()
            // A dedicated thread owns the complete native transport lifetime.
            // Snapshot/stop calls only read shared state or set atomic intent.
            let captureThread = Thread {
                var completion = "{\"schema\":1,\"ok\":false,\"error\":\"Worker did not complete\"}"
                defer { current.finish(completion) }
                let activity = ProcessInfo.processInfo.beginActivity(options: .userInitiated, reason: "Receiving tethered Valeo photographs")
                defer { ProcessInfo.processInfo.endActivity(activity) }
                var matrix = metadata.matrix, neutral = metadata.neutral
                completion = folder.withCString { directory in metadata.bodySerial.withCString { serial in
                    matrix.withUnsafeMutableBufferPointer { m in neutral.withUnsafeMutableBufferPointer { n in
                        profile.withUnsafeBytes { bytes in
                            bridgeValue(VCRunCapture(id, directory, UInt32(iso), UInt32(orientation), metadata.whiteLevel,
                                m.baseAddress, n.baseAddress, serial, bytes.baseAddress, UInt64(profile.count), saveTIFF ? 1 : 0))
                        }
                    }}
                }}
            }
            captureThread.name = "ValeoCapture camera and export"
            captureThread.qualityOfService = .userInitiated
            captureThread.start()
            reply(reserved)
        } catch {
            let data = try? JSONSerialization.data(withJSONObject: ["schema": 1, "ok": false, "error": error.localizedDescription])
            reply(data.flatMap { String(data: $0, encoding: .utf8) } ?? "{}")
        }
    }
    func validateProfile(_ data: Data, reply: @escaping (String) -> Void) {
        worker.async { reply(data.withUnsafeBytes { bridgeValue(VCValidateProfileData($0.baseAddress, UInt64($0.count))) }) }
    }
    func syntheticDNG(_ newPath: String, reply: @escaping (String) -> Void) {
        worker.async { reply(newPath.withCString { bridgeValue(VCWriteSyntheticDNG($0)) }) }
    }
}

final class Listener: NSObject, NSXPCListenerDelegate {
    private let service = CaptureService()
    func listener(_ listener: NSXPCListener, shouldAcceptNewConnection connection: NSXPCConnection) -> Bool {
        guard connection.effectiveUserIdentifier == geteuid() else { return false }
        connection.exportedInterface = NSXPCInterface(with: ValeoCaptureServiceProtocol.self)
        connection.exportedObject = service
        connection.resume()
        return true
    }
}

if CommandLine.arguments.contains("--self-test") {
    print(bridgeValue(VCDiscover()))
} else {
    let delegate = Listener()
    let listener = NSXPCListener.service()
    listener.delegate = delegate
    listener.resume()
}
