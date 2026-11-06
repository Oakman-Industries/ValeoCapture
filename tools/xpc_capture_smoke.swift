// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation

@main struct CaptureServiceSmoke {
    static func main() throws {
        guard let calibration = CameraCalibration.bundled(), let resources = Bundle.main.resourceURL,
              let file = try? FileHandle(forReadingFrom: resources.appendingPathComponent("LeafProfiles/LF2 Portrait4.ICC")) else {
            fputs("Functional resources unavailable\n", stderr); exit(1)
        }
        let profile = file.readData(ofLength: 16 * 1024 * 1024 + 1); file.closeFile()
        let folder = FileManager.default.temporaryDirectory.appendingPathComponent("valeo-service-smoke-" + UUID().uuidString)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: false)
        defer { try? FileManager.default.removeItem(at: folder) }
        let connection = NSXPCConnection(serviceName: captureServiceName)
        connection.remoteObjectInterface = NSXPCInterface(with: ValeoCaptureServiceProtocol.self)
        connection.resume(); defer { connection.invalidate() }
        var finished = false, success = false
        func value(_ s: String) -> [String: Any] { (try? JSONSerialization.jsonObject(with: Data(s.utf8))) as? [String: Any] ?? [:] }
        func finish(_ ok: Bool, _ message: String) { DispatchQueue.main.async { success = ok; finished = true; print(message) } }
        guard let service = connection.remoteObjectProxyWithErrorHandler({ error in finish(false, error.localizedDescription) }) as? ValeoCaptureServiceProtocol else { exit(1) }
        // Impossible registry ID exercises real XPC/native worker/teardown without
        // acquiring a device or sending any vendor command, even if one connects.
        let unavailable = String(UInt64.max)
        let supportedISOs = [25, 50, 100, 200]
        var checkedISOs = 0
        func startNext() {
            service.startCapture(unavailable, folder: folder.path, iso: supportedISOs[checkedISOs], orientation: 1, profile: profile,
                                 calibration: calibration.data, saveTIFF: true) { reserved in
                guard value(reserved)["ok"] as? Bool == true else { finish(false, reserved); return }
                service.waitForCaptureToFinish { ended in
                    guard value(ended)["ok"] as? Bool == false, (value(ended)["error"] as? String)?.contains("candidate disappeared") == true else {
                        finish(false, ended); return
                    }
                    poll()
                }
            }
        }
        func poll() {
            service.captureState { json in
                let state = value(json)
                guard state["schema"] as? Int == 1, let active = state["active"] as? Bool else { finish(false, json); return }
                if active { DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { poll() }; return }
                guard let error = state["error"] as? String, error.contains("candidate disappeared") else { finish(false, json); return }
                service.stopCapture { stopped in
                    guard value(stopped)["ok"] as? Bool == true else { finish(false, stopped); return }
                    checkedISOs += 1
                    if checkedISOs < supportedISOs.count { startNext(); return }
                    finish(true, "{\"ok\":true,\"hardwareCommandsSent\":false,\"checks\":[\"invalid ISO\",\"ISO 25/50/100/200 preflight\",\"dedicated native worker\",\"missing-device error state\",\"stop after failure\"]}")
                }
            }
        }
        service.requestSoftwareISO(100) { inactive in
            guard value(inactive)["ok"] as? Bool == false else { finish(false, "Inactive gain request accepted"); return }
        service.startCapture(unavailable, folder: folder.path, iso: 100001, orientation: 1, profile: profile,
                             calibration: calibration.data, saveTIFF: true) { rejected in
            guard value(rejected)["ok"] as? Bool == false else { finish(false, "Invalid ISO accepted"); return }
            startNext()
        }
        }
        let deadline = Date().addingTimeInterval(15)
        while !finished && Date() < deadline { RunLoop.current.run(until: Date().addingTimeInterval(0.05)) }
        if !finished { fputs("Capture service smoke timed out\n", stderr) }
        exit(success ? 0 : 1)
    }
}
