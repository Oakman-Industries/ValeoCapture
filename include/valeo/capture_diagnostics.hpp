// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "valeo/transport.hpp"
#include <cstdint>
#include <string>

namespace valeo {
// A light frame whose capture parameters (op 0x22, read before the frame)
// report no exposure. Measured on a Valeo 11 (VA11A/5/C, firmware 3301):
// firing the body with the shutter/sync cable disconnected makes the back beep
// four times and announce a normal frame (its ready status is byte-identical
// to a good frame's), but capture-parameter word 11 (offset 0x2C) is 0 and the
// frame is 21,772,608 bytes of 0xFFFF. In every good light and auxiliary frame
// on record word 11 is non-zero, and an auxiliary frame repeats its light
// frame's value, so it reads as the measured exposure time. Word 9 is not a
// reliable flag (it is 0 in many good frames).
inline constexpr std::size_t kCaptureParametersExposureOffset = 0x2C;
[[nodiscard]] bool capture_parameters_report_exposure(const Bytes& capture_parameters);

// Counts 16-bit big-endian samples that are not 0xFFFF (the back's empty-frame
// fill). An odd trailing byte counts as one sample unless it is 0xFF.
[[nodiscard]] std::uint64_t count_non_empty_samples(const Bytes& data);

struct FailedExposureRecord {
    std::string time;            // ISO 8601, local time with offset
    std::uint64_t frame_bytes{};  // bytes drained from the back
    std::uint64_t non_empty_samples{};
    std::uint32_t bus_generation{};
    std::uint16_t software_iso{};
    Bytes ready_status, capture_parameters;
    // The auxiliary frame ValeoCapture then requests to finish the back's cycle.
    std::uint64_t auxiliary_non_empty_samples{};
    Bytes auxiliary_parameters;
};
inline constexpr const char* kFailedExposureMessage =
    "The back fired but recorded no exposure. Check that the shutter/sync cable between the lens and the back "
    "is connected, then fire again. ValeoCapture is still listening.";
// The JSON written next to the captures for a failed exposure. Holds the
// back's status and capture-parameter bytes (hex) for diagnosis, no image data.
[[nodiscard]] std::string failed_exposure_json(const FailedExposureRecord&);
} // namespace valeo
