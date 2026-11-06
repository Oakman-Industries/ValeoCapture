// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/image_writer.hpp"
#include <tiffio.h>
#include <iostream>
#include <unistd.h>
#include <limits>
#include <algorithm>
#include <cstdio>
#include <vector>
using namespace valeo;
int main(){
    char temp[]="/tmp/valeo-image-tests-XXXXXX";auto dir=::mkdtemp(temp);if(!dir)return 1;
    auto path=std::filesystem::path(dir)/"synthetic.dng";auto m=synthetic_metadata();auto pixels=synthetic_mosaic();
    try{
        m.orientation=6;m.iso=100;m.baseline_exposure_ev=2;m.unique_camera_model="ValeoCapture Test Fixture";m.active_area={2,4,62,60};write_dng(path,m,pixels);
        TIFF* t=TIFFOpen(path.c_str(),"r");if(!t)throw std::runtime_error("Independent libtiff rejected DNG");
        std::uint32_t w=0,h=0;std::uint16_t bits=0,photo=0,orientation=0;
        TIFFGetField(t,TIFFTAG_IMAGEWIDTH,&w);TIFFGetField(t,TIFFTAG_IMAGELENGTH,&h);TIFFGetField(t,TIFFTAG_BITSPERSAMPLE,&bits);TIFFGetField(t,TIFFTAG_PHOTOMETRIC,&photo);TIFFGetField(t,TIFFTAG_ORIENTATION,&orientation);
        if(w!=64||h!=64||bits!=16||photo!=32803||orientation!=6)throw std::runtime_error("Independent metadata check failed");
        float exposure=0;
        if(!TIFFGetField(t,TIFFTAG_BASELINEEXPOSURE,&exposure) || exposure!=2)
            throw std::runtime_error("DNG software gain rendering exposure missing or incorrectly encoded");
        char* unique=nullptr;char* model=nullptr;
        if(!TIFFGetField(t,TIFFTAG_UNIQUECAMERAMODEL,&unique) || std::string(unique)!="ValeoCapture Test Fixture" ||
           !TIFFGetField(t,TIFFTAG_MODEL,&model) || std::string(model)!=m.model)
            throw std::runtime_error("Converted-DNG identity changed physical camera model");
        float *origin=nullptr,*crop=nullptr;std::uint32_t *area=nullptr;
        if(!TIFFGetField(t,TIFFTAG_DEFAULTCROPORIGIN,&origin)||!TIFFGetField(t,TIFFTAG_DEFAULTCROPSIZE,&crop)||!TIFFGetField(t,TIFFTAG_ACTIVEAREA,&area)||
           origin[0]!=0||origin[1]!=0||crop[0]!=56||crop[1]!=60||area[0]!=2||area[1]!=4||area[2]!=62||area[3]!=60)
            throw std::runtime_error("Crop is not relative to nonzero ActiveArea");
        std::vector<std::uint16_t> line(w);
        for(std::uint32_t row=0;row<h;++row){if(TIFFReadScanline(t,line.data(),row,0)<0)throw std::runtime_error("TIFF strip decode failed");for(std::uint32_t x=0;x<w;++x)if(line[x]!=pixels[row*w+x])throw std::runtime_error("Raw pixel mismatch");}
        std::uint32_t markSize=0;void* markData=nullptr;
        if(!TIFFGetField(t,TIFFTAG_DNGPRIVATEDATA,&markSize,&markData)||!markData||
           std::string(static_cast<const char*>(markData),markSize)!=std::string(provenance_owner)+'\0'+provenance_payload+'\0')
            throw std::runtime_error("Independent decoder did not find the provenance mark");
        std::uint64_t exif=0;if(!TIFFGetField(t,TIFFTAG_EXIFIFD,&exif)||!TIFFReadEXIFDirectory(t,exif))throw std::runtime_error("Invalid EXIF IFD");
        TIFFClose(t);
        if(!has_provenance(path))throw std::runtime_error("fingerprint check missed the DNG mark");
        {   // A file without the mark (or with a different payload) must not pass.
            const auto copy=std::filesystem::path(dir)/"unmarked.dng";std::filesystem::copy_file(path,copy);
            std::FILE* f=std::fopen(copy.c_str(),"r+b");if(!f)throw std::runtime_error("copy open failed");
            std::vector<char> all(std::filesystem::file_size(copy));if(std::fread(all.data(),1,all.size(),f)!=all.size())throw std::runtime_error("copy read failed");
            const std::string needle=provenance_payload;auto it=std::search(all.begin(),all.end(),needle.begin(),needle.end());
            if(it==all.end())throw std::runtime_error("payload not found in file bytes");
            *it='X';std::fseek(f,static_cast<long>(it-all.begin()),SEEK_SET);std::fputc('X',f);std::fclose(f);
            if(has_provenance(copy))throw std::runtime_error("fingerprint check accepted an altered mark");
        }
        auto invalid=m;invalid.xyz_to_camera={};bool refused=false;try{write_dng(std::filesystem::path(dir)/"invalid.dng",invalid,pixels);}catch(const Error&){refused=true;}
        if(!refused||std::filesystem::exists(std::filesystem::path(dir)/"invalid.dng"))throw std::runtime_error("Unknown color metadata was accepted");
        invalid=m;invalid.xyz_to_camera={1,0,0,0,1,0,0,0,0.0000001};refused=false;try{write_dng(std::filesystem::path(dir)/"rounded.dng",invalid,pixels);}catch(const Error&){refused=true;}
        if(!refused)throw std::runtime_error("Color matrix became singular after encoding");
        invalid=m;invalid.baseline_exposure_ev=std::numeric_limits<double>::quiet_NaN();refused=false;
        try{write_dng(std::filesystem::path(dir)/"exposure.dng",invalid,pixels);}catch(const Error&){refused=true;}
        if(!refused || std::filesystem::exists(std::filesystem::path(dir)/"exposure.dng.partial"))throw std::runtime_error("Nonfinite exposure reached the writer");
        std::filesystem::remove_all(dir);std::cout<<"PASS independent TIFF decoder: metadata, orientation, EXIF, provenance mark and every raw sample; unknown color matrix refused\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(dir);return 1;}
}
