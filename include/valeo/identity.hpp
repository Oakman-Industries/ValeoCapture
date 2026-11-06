// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
enum class Parameter { camera_type, serial_revision, body_serial, sensor_size, sensor_color, firmware_version };
std::string parameter_name(Parameter);
// A temporary named-read selector, not a setting or persistent write. The
// observed legacy sender leaks uninitialized padding; our selector is zeroed.
Command parameter_selector(Parameter,std::uint32_t generation);
bool is_parameter_selector(const Command&);
struct Identity {
    std::string camera_type, revision, body_serial, sensor_size, sensor_color;
    bool is_valeo11() const noexcept;
};
class IdentityReader {
    Transport& transport_;
    std::uint32_t generation_;
    Bytes query(Parameter);
public:
    explicit IdentityReader(Transport& t):transport_(t),generation_(t.generation()){}
    std::string read(Parameter);
    // Unlike the five identity strings, this parameter starts with binary BE32.
    // The word reads as major=word/1000 and minor=word%1000; family compatibility
    // cannot be established by comparing these numbers alone.
    std::uint32_t firmware_version();
    Identity identify();
};
} // namespace valeo
