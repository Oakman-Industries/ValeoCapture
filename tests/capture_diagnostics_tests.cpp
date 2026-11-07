// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_diagnostics.hpp"
#include <iostream>
#include <stdexcept>

using namespace valeo;
void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
Bytes parameters_with_word11(std::uint32_t value) {
    Bytes p(480,0);
    p[0x2C]=static_cast<std::uint8_t>(value>>24);p[0x2D]=static_cast<std::uint8_t>(value>>16);
    p[0x2E]=static_cast<std::uint8_t>(value>>8);p[0x2F]=static_cast<std::uint8_t>(value);
    p[0x24]=1; // word 9 is not the signal: set it on the failed case too
    return p;
}
int main(){try{
    // Values from real Valeo 11 frames: good light 0x0002e1da, its dark frame repeats it;
    // shutter fired without the sync cable: 0.
    require(capture_parameters_report_exposure(parameters_with_word11(0x0002e1da)),"Good frame read as failed");
    require(capture_parameters_report_exposure(parameters_with_word11(0x00142f1e)),"Long exposure read as failed");
    require(!capture_parameters_report_exposure(parameters_with_word11(0)),"No-exposure frame not detected");
    bool truncated=false;try{(void)capture_parameters_report_exposure(Bytes(0x2C,0));}catch(const Error&){truncated=true;}
    require(truncated,"Truncated capture parameters were accepted");

    require(count_non_empty_samples(Bytes(64,0xFF))==0,"Empty fill counted as data");
    require(count_non_empty_samples({0xFF,0xFF,0x3F,0xFF,0x00,0x00})==2,"Real samples not counted");
    require(count_non_empty_samples({0xFF,0xFE})==1,"Near-empty sample not counted");
    require(count_non_empty_samples({0xFF,0xFF,0x12})==1 && count_non_empty_samples({0xFF})==0,"Odd tail mishandled");

    FailedExposureRecord r;r.time="2026-11-07T18:46:52+00:00";r.frame_bytes=21772608;r.non_empty_samples=0;
    r.bus_generation=8;r.software_iso=25;r.ready_status={0x01,0x4c,0x3a,0x40};r.capture_parameters={0x00,0xab};
    r.auxiliary_non_empty_samples=10886304;r.auxiliary_parameters={0xcd};
    const auto json=failed_exposure_json(r);
    require(json.find("\"kind\":\"failed-exposure\"")!=std::string::npos,"Kind missing");
    require(json.find("\"readyStatus\":\"014c3a40\"")!=std::string::npos,"Status hex missing");
    require(json.find("\"captureParameters\":\"00ab\"")!=std::string::npos,"Parameter hex missing");
    require(json.find("\"frameBytes\":21772608")!=std::string::npos && json.find("\"nonEmptySamples\":0")!=std::string::npos,"Counts missing");
    require(json.find("\"auxiliaryNonEmptySamples\":10886304")!=std::string::npos &&
            json.find("\"auxiliaryParameters\":\"cd\"")!=std::string::npos,"Auxiliary fields missing");
    require(json.find("sync cable")!=std::string::npos && json.back()=='\n',"Message or newline missing");
    std::cout<<"PASS capture diagnostics: exposure word, empty-frame count, failed-exposure record\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
