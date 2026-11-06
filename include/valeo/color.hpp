// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
struct RenderedRgb {std::vector<std::uint16_t> pixels;Bytes output_icc;};
Bytes srgb_profile();
void validate_rgb_profile(const Bytes&);
// Encoding of the supplied Leaf preset's RGB inputs, read from its three
// single-gamma TRCs. Unsupported curve forms are rejected, never approximated.
std::array<double,3> rgb_profile_gamma(const Bytes&);
// Caller supplies calibrated, demosaiced camera RGB in the input profile's
// expected encoding. This is a color transform, not profile reassignment.
RenderedRgb render_profiled_rgb(std::uint32_t width,std::uint32_t height,
                               const std::vector<std::uint16_t>& camera_rgb,const Bytes& input_icc);
} // namespace valeo
