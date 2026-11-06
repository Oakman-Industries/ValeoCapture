// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/protocol.hpp"
#include <limits>

namespace valeo {
bool Device::leaf_candidate() const noexcept {
    return vendor == 0x6cd && command_set_spec == 0x609e && command_set == 0x104d8 && device_type == 2;
}
std::array<std::uint8_t,6> make_cdb(Direction direction, std::uint8_t operation, std::uint32_t size) {
    if (size < 2 || size > max_command_bytes)
        throw Error(ErrorCode::invalid, "Leaf command transfer must be 2..0xffffff bytes; zero-payload scratch semantics require explicit handling");
    return {static_cast<std::uint8_t>(direction == Direction::from_device ? 0x28 : 0x0f), operation,
            static_cast<std::uint8_t>(size >> 16), static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size), 0};
}
void Command::validate() const {
    if (cdb[0] != (direction == Direction::from_device ? 0x28 : 0x0f) || cdb[5] != 0)
        throw Error(ErrorCode::invalid, "Not a supported Leaf six-byte command");
    const auto n = (std::uint32_t(cdb[2]) << 16) | (std::uint32_t(cdb[3]) << 8) | cdb[4];
    if (n < 2 || (direction == Direction::from_device ? incoming_size != n || !outgoing.empty() : outgoing.size() != n || incoming_size != 0))
        throw Error(ErrorCode::invalid, "CDB length, direction and owned payload disagree");
    if (timeout.count() < 1 || timeout.count() > 120000)
        throw Error(ErrorCode::invalid, "Command timeout must be 1..120000 ms");
}
bool valeo11_family(std::string_view serial) noexcept {
    return serial.size() >= 5 && serial.substr(0,4) == "VA11" && (serial[4] == 'A' || serial[4] == 'M');
}
bool valid_valeo11_serial(std::string_view serial) noexcept {
    // Bounded equivalent of the reference model classifier, not an authenticity check.
    return serial.size() >= 46 && valeo11_family(serial) && serial[44] >= '0' && serial[44] <= '9'
        && (serial[45] == 'C' || serial[45] == 'M');
}
std::uint16_t read_be16(const Bytes& b, std::size_t o) {
    if (o > b.size() || b.size() - o < 2) throw Error(ErrorCode::protocol,"Truncated BE16 field");
    return static_cast<std::uint16_t>((std::uint16_t(b[o]) << 8) | b[o+1]);
}
std::uint32_t read_be32(const Bytes& b, std::size_t o) {
    if (o > b.size() || b.size() - o < 4) throw Error(ErrorCode::protocol,"Truncated BE32 field");
    return (std::uint32_t(b[o]) << 24) | (std::uint32_t(b[o+1]) << 16) | (std::uint32_t(b[o+2]) << 8) | b[o+3];
}
JpegLsEnvelope parse_jpegls_envelope(const Bytes& b, std::uint32_t capacity) {
    constexpr std::uint32_t marker = 0xffd8fff7;
    if (b.size() < 8) throw Error(ErrorCode::protocol,"Truncated JPEG-LS envelope");
    std::size_t prefix;
    std::uint32_t size;
    if (read_be32(b,4) == marker) { prefix=4; size=read_be32(b,0); }
    else if (b.size() >= 12 && read_be32(b,8) == marker && read_be32(b,0) == 0) { prefix=8; size=read_be32(b,4); }
    else throw Error(ErrorCode::protocol,"Unsupported JPEG-LS prefix or marker");
    if (!size || size > capacity || size % 2) throw Error(ErrorCode::protocol,"JPEG-LS decoded size outside validated pixel capacity");
    return {prefix,size};
}
static void require(bool condition, const char* message) { if (!condition) throw Error(ErrorCode::protocol,message); }
void CaptureState::connected(std::uint32_t g) { require(state_==SessionState::disconnected,"Already connected"); generation_=g; state_=SessionState::candidate; }
void CaptureState::identified(std::string_view s) { require(state_==SessionState::candidate,"Not a candidate"); require(valid_valeo11_serial(s),"Serial fields do not identify a supported Valeo 11"); state_=SessionState::identified; }
void CaptureState::armed() { require(state_==SessionState::identified,"Not identified"); state_=SessionState::armed; }
void CaptureState::receiving(std::uint32_t g) { if (g!=generation_) {state_=SessionState::faulted;throw Error(ErrorCode::stale_generation,"Bus generation changed");} require(state_==SessionState::armed,"Not armed"); state_=SessionState::receiving; }
void CaptureState::committing() { require(state_==SessionState::receiving,"Not receiving"); state_=SessionState::committing; }
void CaptureState::committed() { require(state_==SessionState::committing,"Capture is not durably committed"); state_=SessionState::identified; }
void CaptureState::reset(std::uint32_t g) noexcept { generation_=g;state_=SessionState::faulted; }
} // namespace valeo
