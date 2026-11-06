// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <filesystem>

namespace valeo {
struct RawMetadata {
    std::uint32_t width{}, height{}, black_level{}, white_level{};
    std::array<std::uint8_t,4> cfa{}; // RGB plane indices in a 2x2 tile; explicit, never guessed
    std::array<double,9> xyz_to_camera{};
    std::array<double,3> as_shot_neutral{};
    std::array<std::uint32_t,4> active_area{}; // top,left,bottom,right
    std::uint16_t orientation{1}, iso{}, illuminant{21};
    std::string make, model, serial;
    // DNG default rendering exposure; preserves original mosaic/highlight data.
    double baseline_exposure_ev{};
    // Converted DNG identity may differ from native camera raws while EXIF
    // make/model remain true. Prevent native-camera decoder overrides of tags.
    std::string unique_camera_model;
};
void validate_raw_metadata(const RawMetadata&);
// Input is the already validated/corrected mosaic. Sensor corrections are NOT
// inferred by this writer. No tone curve, white balance or orientation is baked.
void write_dng(const std::filesystem::path&, const RawMetadata&, const std::vector<std::uint16_t>&);
// RGB samples must already be in the supplied OUTPUT ICC profile's color space.
// Do not label camera-native RGB with an output profile without a color transform.
void write_rgb_tiff(const std::filesystem::path&, std::uint32_t width, std::uint32_t height,
                    std::uint16_t orientation, const std::vector<std::uint16_t>& rgb, const Bytes& output_icc);
// Provenance: every DNG/TIFF this writer emits carries one extra IFD0 entry,
// DNGPrivateData (tag 50740, BYTE) = "Oakman Industries\0" + payload + "\0".
// Invisible and non-destructive: no pixel sample or other tag value changes;
// readers that do not know the tag ignore it.
inline constexpr std::uint16_t provenance_tag = 50740;
inline constexpr const char* provenance_owner = "Oakman Industries";
inline constexpr const char* provenance_payload = "oakman-industries/ValeoCapture";
// True only when the file's first IFD holds exactly the mark above.
bool has_provenance(const std::filesystem::path&);
RawMetadata synthetic_metadata();
std::vector<std::uint16_t> synthetic_mosaic();
} // namespace valeo
