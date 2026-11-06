// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation

// Value-only, versioned IPC. No raw C++ objects, addresses, or shell commands.
@objc(ValeoCaptureServiceProtocol)
protocol ValeoCaptureServiceProtocol {
    func snapshot(_ reply: @escaping (String) -> Void)
    func probe(_ registryID: String, reply: @escaping (String) -> Void)
    func inspect(_ registryID: String, reply: @escaping (String) -> Void)
    func startCapture(_ registryID: String, folder: String, iso: Int, orientation: Int,
                      profile: Data, calibration: Data, saveTIFF: Bool, reply: @escaping (String) -> Void)
    func captureState(_ reply: @escaping (String) -> Void)
    // An outstanding request owns the session's activity/importance until the
    // native worker has drained and closed. Never retain it after completion.
    func waitForCaptureToFinish(_ reply: @escaping (String) -> Void)
    func stopCapture(_ reply: @escaping (String) -> Void)
    func requestSoftwareISO(_ iso: Int, reply: @escaping (String) -> Void)
    func validateProfile(_ data: Data, reply: @escaping (String) -> Void)
    func syntheticDNG(_ newPath: String, reply: @escaping (String) -> Void)
}

let captureServiceName = "org.valeocapture.CaptureService"
