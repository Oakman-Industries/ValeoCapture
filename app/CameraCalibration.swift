// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
import Foundation
import CoreFoundation

// Private user-supplied camera metadata is a resource, never source code or a
// fallback identity matrix. Serial matching prevents reuse on a different back.
struct CameraCalibration {
    let data: Data
    let bodySerial: String
    let matrix: [Double]
    let neutral: [Double]
    let whiteLevel: UInt32
    init(_ data: Data) throws {
        func invalid() -> NSError { NSError(domain: "ValeoCapture", code: 1, userInfo: [NSLocalizedDescriptionKey: "A reviewed calibration for this Valeo 11 back is required."]) }
        guard data.count <= 32 * 1024,
              let value = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              value["schema"] as? Int == 1,
              value["cameraType"] as? String == "VA11A/5/C",
              value["correction"] as? String == "paired-dark-development",
              let serial = value["bodySerial"] as? String, !serial.isEmpty, serial.utf8.count <= 64,
              let white = value["whiteLevel"] as? NSNumber,
              CFGetTypeID(white) != CFBooleanGetTypeID(), white.doubleValue == Double(white.uint32Value),
              white.uint32Value > 0, white.uint32Value <= 16383 else { throw invalid() }
        func numbers(_ key: String, _ count: Int) throws -> [Double] {
            guard let array = value[key] as? [NSNumber], array.count == count else { throw invalid() }
            let result = array.map { $0.doubleValue }
            guard zip(array, result).allSatisfy({ CFGetTypeID($0.0) != CFBooleanGetTypeID() && $0.1.isFinite && abs($0.1) <= 1000 }) else { throw invalid() }
            return result
        }
        let matrix = try numbers("xyzToCamera", 9), neutral = try numbers("asShotNeutral", 3)
        guard neutral.allSatisfy({ $0 > 0 && $0 <= 1 }) else { throw invalid() }
        let c = matrix.map { ($0 * 1_000_000).rounded() / 1_000_000 }
        let det = c[0] * (c[4] * c[8] - c[5] * c[7]) - c[1] * (c[3] * c[8] - c[5] * c[6]) + c[2] * (c[3] * c[7] - c[4] * c[6])
        guard abs(det) >= 1e-10 else { throw invalid() }
        self.data = data; self.bodySerial = serial; self.matrix = matrix; self.neutral = neutral; self.whiteLevel = white.uint32Value
    }
    static func bundled() -> CameraCalibration? {
        guard let url = Bundle.main.resourceURL?.appendingPathComponent("LeafCalibration.json"),
              let file = try? FileHandle(forReadingFrom: url) else { return nil }
        defer { file.closeFile() }
        return try? CameraCalibration(file.readData(ofLength: 32 * 1024 + 1))
    }
}
