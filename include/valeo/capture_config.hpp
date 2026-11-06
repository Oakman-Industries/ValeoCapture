// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
namespace valeo {
constexpr std::uint32_t reference_width=4056,reference_height=2684;
constexpr std::uint32_t reference_frame_bytes=reference_width*reference_height*2;
// Valeo 11 ISO selector values. 25/50 were observed on the wire; 100/200 are
// software-gain settings verified as digital gain. Qualify each added ISO live.
constexpr std::array<std::uint16_t,4> valeo11_iso_values{25,50,100,200};
// Observed VA11A / sensor-size-code 5 / color C physical-shutter configuration.
// It neither triggers a shutter nor requests the auxiliary calibration frame.
Command physical_arm(std::uint32_t generation);
bool is_reference_physical_arm(const Command&);
enum class PhysicalSetup { release_interface, rapid_mode, ready_beep };
// Exact nonpersistent startup requests observed in the reference. Values are
// intentionally fixed; this is not a general camera-setting API.
Command physical_setup(PhysicalSetup,std::uint32_t generation);
bool is_reference_physical_setup(const Command&);
// Statically recovered auxiliary contract: same geometry, frame type2; the
// operation0x13 handler ignores payload bytes. Retain observed56-byte framing
// with initialized padding, never serialize the legacy host-pointer scratch.
Command auxiliary_arm(std::uint32_t generation);
Command auxiliary_trigger(std::uint32_t generation);
Command timed_light_rearm(std::uint32_t generation,std::uint32_t status_time);
bool is_reference_auxiliary_arm(const Command&);
bool is_reference_auxiliary_trigger(const Command&);
Command select_verified_iso(std::uint16_t iso,std::uint32_t generation);
bool is_reference_iso(const Command&);
} // namespace valeo
