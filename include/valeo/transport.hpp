// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include <array>
#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>

namespace valeo {
using Bytes = std::vector<std::uint8_t>;
enum class Direction { from_device, to_device };
enum class ErrorCode { invalid, disconnected, busy, timeout, stale_generation, protocol, io, unsupported, cancelled };
class Error : public std::runtime_error {
public:
    ErrorCode code;
    Error(ErrorCode c, const std::string& message) : std::runtime_error(message), code(c) {}
};
struct Device {
    std::uint64_t registry_id{}, guid{};
    std::uint32_t vendor{}, command_set_spec{}, command_set{}, device_type{};
    std::string path;
    bool leaf_candidate() const noexcept;
};
struct Command {
    std::array<std::uint8_t, 6> cdb{};
    Direction direction{Direction::from_device};
    Bytes outgoing;
    std::uint32_t incoming_size{}, generation{};
    std::chrono::milliseconds timeout{3000};
    void validate() const;
};
struct Reply {
    Bytes data;
    std::uint32_t generation{};
    std::uint8_t sbp_status{}, response{};
    // Host-only diagnostics, never serialized onto the bus. Zero on backends
    // without instrumentation; synchronous wait includes driver and bus time.
    double prepare_seconds{},execute_seconds{},completion_seconds{};
};
class Transport {
public:
    virtual ~Transport() = default;
    virtual std::vector<Device> enumerate() = 0;
    virtual void open(std::uint64_t registry_id) = 0;
    virtual Reply submit(const Command&) = 0;
    virtual void close() noexcept = 0;
    virtual std::uint32_t generation() const noexcept = 0;
    // Backend DMA ceiling; it may be lower than Leaf's 512 KiB maximum.
    virtual std::uint32_t frame_chunk_limit() const noexcept{return 0x80000;}
    virtual bool paired_capture_enabled() const noexcept{return false;}
};
} // namespace valeo
