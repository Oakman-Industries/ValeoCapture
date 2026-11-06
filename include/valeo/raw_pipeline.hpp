// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "image_writer.hpp"

namespace valeo {
// Original Valeo11 software ISO:1x/2x/4x/8x relative to base25. This is
// processing exposure, never a sensor/ADC setting or additional random noise.
double valeo11_software_exposure_stops(std::uint16_t effective_iso);
// VA11A/5/C geometry observed against the paired Leaf MOS: crop24/6 then
// normalize the sensor's180-degree wire layout to stored-MOS BGGR order.
// Applies fresh paired dark subtraction only. Factory gain/defect equivalence
// is a separate, still-unqualified stage; received planes must be preserved.
std::vector<std::uint16_t> subtract_valeo11_pair(const Bytes& light,const Bytes& auxiliary);
// Explicit camera metadata and ICC input encoding are required. User rotation
// remains metadata; it is not applied to the mosaic by this renderer.
std::vector<std::uint16_t> demosaic_camera_rgb(const std::vector<std::uint16_t>& mosaic,
    const RawMetadata&,const std::array<double,3>& input_gamma);
} // namespace valeo
