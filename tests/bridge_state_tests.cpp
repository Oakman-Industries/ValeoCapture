// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "bridge.h"
#include <iostream>
#include <stdexcept>
#include <string>

static std::string value(char* p) {
    if(!p)throw std::runtime_error("Missing bridge response");
    std::string s(p);VCFree(p);return s;
}
static void require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
int main(){try{
    require(value(VCRequestSoftwareISO(100)).find("\"ok\":false")!=std::string::npos,"Inactive ISO request accepted");
    require(value(VCReserveCapture(25)).find("\"ok\":true")!=std::string::npos,"Session reservation failed");
    require(value(VCRequestSoftwareISO(200)).find("\"requestedISO\":200")!=std::string::npos,"Valid ISO request not queued");
    require(value(VCRequestSoftwareISO(400)).find("\"ok\":false")!=std::string::npos,"Unsupported ISO queued");
    require(value(VCRequestSoftwareISO(65561)).find("\"ok\":false")!=std::string::npos,"Wide ISO truncated to25");
    const auto state=value(VCCaptureState());
    require(state.find("\"requestedISO\":200")!=std::string::npos && state.find("\"softwareISO\":0")!=std::string::npos &&
        state.find("\"isoMode\":\"software\"")!=std::string::npos && state.find("\"sensorGainVerified\":false")!=std::string::npos,"Queued software setting falsely reported selected/gain verified");
    value(VCStopCapture());
    require(value(VCRequestSoftwareISO(100)).find("\"ok\":false")!=std::string::npos,"Stopping session accepted new ISO");
    // Release reservation through invalid-settings failure before any hardware
    // discovery/open. No camera, private calibration or ICC needed by this test.
    value(VCRunCapture(0,nullptr,25,1,0,nullptr,nullptr,nullptr,nullptr,0,0));
    require(value(VCCaptureState()).find("\"active\":false")!=std::string::npos,"Failed worker retained session");
    require(value(VCReserveCapture(65561)).find("\"ok\":false")!=std::string::npos,"Invalid reservation accepted");
    std::cout<<"PASS ISO intent: inactive/invalid/stop rejection, pending versus applied, no hardware\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
