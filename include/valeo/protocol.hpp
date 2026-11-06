// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <string_view>

namespace valeo {
constexpr std::uint32_t max_command_bytes = 0x00ffffff;
// Leaf vendor format: opcode 0x28 is six bytes here, not standard READ(10).
std::array<std::uint8_t,6> make_cdb(Direction, std::uint8_t operation, std::uint32_t size);
bool valeo11_family(std::string_view serial) noexcept;
bool valid_valeo11_serial(std::string_view serial) noexcept;
std::uint16_t read_be16(const Bytes&, std::size_t offset);
std::uint32_t read_be32(const Bytes&, std::size_t offset);
struct JpegLsEnvelope {
    std::size_t prefix_size{};
    std::uint32_t decoded_bytes{};
};
JpegLsEnvelope parse_jpegls_envelope(const Bytes&, std::uint32_t capacity);
enum class SessionState { disconnected, candidate, identified, armed, receiving, committing, faulted };
class CaptureState {
    SessionState state_{SessionState::disconnected};
    std::uint32_t generation_{};
public:
    SessionState state() const noexcept { return state_; }
    void connected(std::uint32_t generation);
    void identified(std::string_view serial);
    void armed();
    void receiving(std::uint32_t generation);
    void committing();
    void committed();
    void reset(std::uint32_t generation) noexcept;
    void disconnected() noexcept { state_ = SessionState::disconnected; }
};
} // namespace valeo
