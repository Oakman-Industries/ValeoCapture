// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <memory>

namespace valeo {
// Apple SCSITaskLib path for a Leaf LUN already owned by the generic transport.
// No matcher installation. Registry IDs refer to the parent SBP2 LUN, matching
// LegacyTransport discovery. FireWire generation is read before/after each task.
class ScsiTransport final:public Transport {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    ScsiTransport();
    ~ScsiTransport() override;
    std::vector<Device> enumerate() override;
    void open(std::uint64_t registry_id) override;
    Reply submit(const Command&) override;
    void close() noexcept override;
    std::uint32_t generation() const noexcept override;
    std::uint32_t frame_chunk_limit() const noexcept override{return 61440;}
    bool paired_capture_enabled() const noexcept override;
};
} // namespace valeo
