// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
// Bounded, native replacement for Leaf's LP64-broken calibration decoder.
// Source is a legacy 4-byte or supported 8-byte length prefix plus JPEG-LS.
std::vector<std::uint16_t> decode_gain_jpegls(const Bytes&,std::uint32_t width,std::uint32_t height);
} // namespace valeo
