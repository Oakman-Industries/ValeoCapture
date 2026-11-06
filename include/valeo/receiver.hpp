// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "protocol.hpp"
#include <filesystem>
#include <functional>

namespace valeo {
struct ReceivePlan {
    std::uint32_t generation{};
    std::uint64_t frame_bytes{};
    std::chrono::milliseconds chunk_timeout{35000};
};
struct ReceiveReceipt { std::uint64_t bytes{}; std::uint32_t chunks{}, generation{}; };
// Downloads opaque, uncalibrated bytes. Caller must establish identity, framing,
// size and arming first. Native transport currently disallows this operation.
// Never acknowledges/deletes a camera frame, triggers, or emits a camera DNG.
ReceiveReceipt receive_frame(Transport&, CaptureState&, const ReceivePlan&,
    const std::filesystem::path& new_path,
    const std::function<void(std::uint64_t,std::uint64_t)>& progress = {},
    const std::function<bool()>& cancelled = {});
} // namespace valeo
