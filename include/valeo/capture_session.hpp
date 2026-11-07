// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "image_writer.hpp"
#include <atomic>
#include <functional>

namespace valeo {
struct CaptureSettings {
    std::uint64_t registry_id{};
    std::filesystem::path folder;
    std::uint16_t software_iso{25};
    RawMetadata camera;
    Bytes input_icc;
    std::string expected_body_serial;
    bool save_tiff{true};
    bool reuse_calibration{};
};
struct CaptureProgress {
    std::string phase{"idle"},stage{"idle"},last_dng,last_tiff,last_raw,error,timing;
    std::string export_phase;
    // Set after the back fired but recorded no exposure (capture_diagnostics.hpp);
    // cleared by the next good image. last_diagnostic is the JSON record's path.
    std::string warning,last_diagnostic;
    std::uint64_t failed_exposures{};
    bool export_pending{};
    bool calibration_reused{};
    double fraction{};
    std::uint64_t received{},total{},completed{};
    std::uint16_t software_iso{},frame_iso{};
};
// One call owns the entire transport/CFRunLoop on its calling thread. Stop is
// intent only: an already-started light/auxiliary drain and export finish first.
void run_capture_session(const CaptureSettings&,const std::atomic_bool& stop,const std::atomic_uint& requested_iso,
    const std::function<void(const CaptureProgress&)>& progress);
} // namespace valeo
