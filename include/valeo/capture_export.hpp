// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "image_writer.hpp"
#include <atomic>
#include <chrono>
#include <future>

namespace valeo {
struct CapturedPair {
    Bytes light,dark;
    RawMetadata metadata;
    std::string stem;
    std::chrono::steady_clock::time_point began;
    double ready_seconds{},photo_read{},photo_journal{},dark_wait{},dark_read{},dark_journal{};
    std::array<double,3> photo_transport{},dark_transport{};
    bool calibration_reused{};
    std::string calibration_source;
};
struct CaptureExportResult {std::string dng,tiff,timing;};
// Exactly one owned pair may be exporting while the transport receives another.
// No camera/plugin/CFRunLoop object may enter the export worker.
class CaptureExporter {
    Bytes icc_;
    std::array<double,3> gamma_;
    bool tiff_;
    std::atomic_uint stage_{0};
    std::future<CaptureExportResult> result_;
public:
    CaptureExporter(Bytes icc,bool save_tiff);
    ~CaptureExporter();
    CaptureExporter(const CaptureExporter&)=delete;
    CaptureExporter& operator=(const CaptureExporter&)=delete;
    void submit(CapturedPair);
    bool busy()const{return result_.valid();}
    bool ready()const;
    const char* phase()const;
    CaptureExportResult take();
};
} // namespace valeo
