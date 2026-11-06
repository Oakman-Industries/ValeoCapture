// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/color.hpp"
#include "valeo/image_writer.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unistd.h>
int main(){try{
    const auto profile=valeo::srgb_profile();valeo::validate_rgb_profile(profile);
    std::vector<std::uint16_t> rgb{0,65535,0, 65535,0,0, 0,0,65535, 32768,16384,49152};
    const auto rendered=valeo::render_profiled_rgb(2,2,rgb,profile);
    for(std::size_t i=0;i<rgb.size();++i)if(std::abs(int(rgb[i])-int(rendered.pixels[i]))>2)throw std::runtime_error("Identity color transform changed channel order, row order or values");
    char name[]="/tmp/valeo-color-XXXXXX";auto dir=mkdtemp(name);if(!dir)return 1;
    valeo::write_rgb_tiff(std::filesystem::path(dir)/"rendered.tif",2,2,1,rendered.pixels,rendered.output_icc);std::filesystem::remove_all(dir);
    bool rejected=false;try{valeo::validate_rgb_profile({1,2,3});}catch(const valeo::Error&){rejected=true;}if(!rejected)throw std::runtime_error("Malformed ICC accepted");
    std::cout<<"PASS CoreGraphics ICC transform: native 16-bit samples, RGB channels/rows, output-profile TIFF, malformed profile refusal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
