// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/jpegls.hpp"
#include "valeo/protocol.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace valeo;
int main(int argc,char** argv){try{
    if(argc!=2)return 2;
    for(const std::string name:{"zero","maximum","ramp","random16"}){
        std::ifstream f(std::filesystem::path(argv[1])/(name+".jls"),std::ios::binary);if(!f)throw std::runtime_error("Missing synthetic fixture");Bytes stream((std::istreambuf_iterator<char>(f)),{});
        for(bool eight:{false,true}){Bytes input=eight?Bytes{0,0,0,0,0,0,0x20,0}:Bytes{0,0,0x20,0};input.insert(input.end(),stream.begin(),stream.end());auto decoded=decode_gain_jpegls(input,64,64);
            std::uint32_t state=0x12345678;for(std::size_t i=0;i<4096;++i){state=1664525u*state+1013904223u;const auto expected=name=="zero"?0u:name=="maximum"?65535u:name=="random16"?state>>16:static_cast<unsigned>(((i%64+i/64)&31)*64);if(decoded[i]!=expected)throw std::runtime_error("Gain pixel mismatch");}
            bool rejected=false;try{decode_gain_jpegls(input,32,64);}catch(const Error&){rejected=true;}if(!rejected)throw std::runtime_error("Geometry mismatch accepted");
            auto lossy=input;bool changed=false;for(std::size_t j=4;j+10<lossy.size();++j)if(lossy[j]==0xff && lossy[j+1]==0xda){lossy[j+7]=1;changed=true;break;}
            if(!changed)throw std::runtime_error("Missing synthetic SOS marker");
            rejected=false;try{decode_gain_jpegls(lossy,64,64);}catch(const Error&){rejected=true;}if(!rejected)throw std::runtime_error("Lossy gain stream accepted");
            input.resize(input.size()/2);rejected=false;try{decode_gain_jpegls(input,64,64);}catch(const Error&){rejected=true;}if(!rejected)throw std::runtime_error("Truncated JPEG-LS accepted");
        }
    }
    std::cout<<"PASS independent FFmpeg fixtures: all gain pixels, both prefix sizes, geometry and truncation checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
