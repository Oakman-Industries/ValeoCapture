// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "bridge.h"
#include "valeo/legacy_transport.hpp"
#include "valeo/host_transport.hpp"
#include "valeo/color.hpp"
#include "valeo/image_writer.hpp"
#include "valeo/identity.hpp"
#include "valeo/capture_session.hpp"
#include "valeo/raw_pipeline.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <functional>
#include <mutex>
#include <atomic>
#include <algorithm>

namespace {
std::string quote(const std::string& s){std::string o="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){o+='\\';o+=static_cast<char>(c);}else if(c<32){const char* h="0123456789abcdef";o+="\\u00";o+=h[c>>4];o+=h[c&15];}else o+=static_cast<char>(c);}return o+'"';}
char* result(const std::function<std::string()>& f){std::string s;try{s=f();}catch(const std::exception& e){s="{\"ok\":false,\"error\":"+quote(e.what())+"}";}s.insert(1,"\"schema\":1,");auto p=static_cast<char*>(std::malloc(s.size()+1));if(p)std::memcpy(p,s.c_str(),s.size()+1);return p;}
std::mutex capture_mutex;
std::atomic_bool capture_active{false},capture_started{false},capture_stop{false};
std::atomic_uint capture_requested_iso{25};
valeo::CaptureProgress capture_progress;
}
extern "C" char* VCDiscover(){return result([]{auto devices=valeo::discover_candidates();std::ostringstream s;s<<"{\"ok\":true,\"captureReady\":false,\"devices\":[";bool first=true;for(auto& d:devices){if(!first)s<<',';first=false;s<<"{\"registryID\":"<<quote(std::to_string(d.registry_id))<<",\"guid\":"<<quote(std::to_string(d.guid))<<",\"path\":"<<quote(d.path)<<'}';}s<<"]}";return s.str();});}
extern "C" char* VCProbe(unsigned long long id){return result([&]{auto choice=valeo::host_transport(id);auto& t=*choice.transport;t.open(id);return "{\"ok\":true,\"captureReady\":false,\"backend\":"+quote(choice.backend)+",\"generation\":"+std::to_string(t.generation())+"}";});}
extern "C" char* VCInspectCamera(unsigned long long id){return result([&]{
    if(capture_active)throw std::runtime_error("Stop capture before inspecting the camera");
    auto choice=valeo::host_transport(id);auto& t=*choice.transport;t.open(id);
    valeo::IdentityReader reader(t);const auto identity=reader.identify();const auto firmware=reader.firmware_version();
    const bool ready=t.paired_capture_enabled() && identity.revision=="A" && identity.sensor_size=="5" && identity.sensor_color=="C" && firmware==3301;
    return "{\"ok\":true,\"captureReady\":"+std::string(ready?"true":"false")+",\"backend\":"+quote(choice.backend)+
        ",\"model\":\"Leaf Valeo 11\",\"bodySerial\":"+quote(identity.body_serial)+",\"firmware\":"+std::to_string(firmware)+",\"softwareISOValues\":[25,50,100,200],\"isoMode\":\"software\"}";
});}
extern "C" char* VCReserveCapture(unsigned int iso){return result([&]{
    std::lock_guard<std::mutex> lock(capture_mutex);
    if(capture_active)throw std::runtime_error("A capture session is already active");
    if(iso>65535)throw std::runtime_error("Invalid ISO");
    valeo::valeo11_software_exposure_stops(static_cast<std::uint16_t>(iso));capture_requested_iso=iso;
    capture_stop=false;capture_started=false;capture_progress={};capture_progress.phase="starting";capture_active=true;
    return std::string("{\"ok\":true}");
});}
extern "C" char* VCRequestSoftwareISO(unsigned int iso){return result([&]{
    std::lock_guard<std::mutex> lock(capture_mutex);
    if(!capture_active || capture_stop)throw std::runtime_error("No listening session accepting settings");
    if(iso>65535)throw std::runtime_error("Invalid ISO");
    valeo::valeo11_software_exposure_stops(static_cast<std::uint16_t>(iso));
    capture_requested_iso=iso;return std::string("{\"ok\":true,\"requestedISO\":")+std::to_string(iso)+"}";
});}
extern "C" char* VCStopCapture(){return result([]{
    std::lock_guard<std::mutex> lock(capture_mutex);capture_stop=true;return std::string("{\"ok\":true}");
});}
extern "C" char* VCCaptureState(){return result([]{
    std::lock_guard<std::mutex> lock(capture_mutex);const auto& p=capture_progress;
    return "{\"ok\":"+std::string(p.error.empty()?"true":"false")+",\"active\":"+(capture_active?"true":"false")+
        ",\"phase\":"+quote(p.phase)+",\"stage\":"+quote(p.stage)+",\"exportPhase\":"+quote(p.export_phase)+
        ",\"exportPending\":"+(p.export_pending?"true":"false")+",\"received\":"+std::to_string(p.received)+",\"total\":"+std::to_string(p.total)+
        ",\"calibrationReused\":"+(p.calibration_reused?"true":"false")+
        ",\"completed\":"+std::to_string(p.completed)+",\"lastDNG\":"+quote(p.last_dng)+",\"lastTIFF\":"+quote(p.last_tiff)+
        ",\"lastRAW\":"+quote(p.last_raw)+",\"fraction\":"+std::to_string(p.fraction)+",\"timing\":"+quote(p.timing)+
        ",\"softwareISO\":"+std::to_string(p.software_iso)+",\"frameISO\":"+std::to_string(p.frame_iso)+",\"requestedISO\":"+std::to_string(capture_requested_iso.load())+
        ",\"isoMode\":\"software\",\"sensorGainVerified\":false,\"error\":"+quote(p.error)+"}";
});}
extern "C" char* VCRunCapture(unsigned long long id,const char* folder,unsigned int iso,unsigned int orientation,unsigned int white,
    const double* matrix,const double* neutral,const char* serial,const void* profile,unsigned long long profile_size,int save_tiff){return result([&]{
    bool expected=false;
    if(!capture_active || !capture_started.compare_exchange_strong(expected,true))throw std::runtime_error("Capture was not reserved or has already started");
    try {
        if(!folder || !serial || !matrix || !neutral || !profile || profile_size<128 || profile_size>16*1024*1024 ||
           iso>65535 ||
           (orientation!=1 && orientation!=3 && orientation!=6 && orientation!=8))throw std::runtime_error("Invalid capture settings");
        valeo::CaptureSettings s;s.registry_id=id;s.folder=folder;s.software_iso=static_cast<std::uint16_t>(iso);s.expected_body_serial=serial;s.save_tiff=save_tiff!=0;
        auto& m=s.camera;m.width=4008;m.height=2672;m.cfa={2,1,1,0};m.active_area={0,0,2672,4008};m.white_level=white;
        m.orientation=static_cast<std::uint16_t>(orientation);m.make="Leaf";m.model="Valeo 11";m.illuminant=21;
        std::copy(matrix,matrix+9,m.xyz_to_camera.begin());std::copy(neutral,neutral+3,m.as_shot_neutral.begin());
        const auto bytes=static_cast<const std::uint8_t*>(profile);s.input_icc.assign(bytes,bytes+profile_size);
        valeo::run_capture_session(s,capture_stop,capture_requested_iso,[](const auto& p){std::lock_guard<std::mutex> lock(capture_mutex);capture_progress=p;});
        std::lock_guard<std::mutex> lock(capture_mutex);capture_active=false;
        return std::string("{\"ok\":true}");
    }catch(const std::exception& error){std::lock_guard<std::mutex> lock(capture_mutex);capture_progress.error=error.what();capture_progress.phase="stopped with error";capture_active=false;throw;}
});}
extern "C" char* VCValidateProfile(const char* path){return result([&]{if(!path)throw std::runtime_error("No profile selected");std::ifstream f(path,std::ios::binary);if(!f)throw std::runtime_error("Cannot open selected ICC profile");f.seekg(0,std::ios::end);auto n=f.tellg();if(n<128||n>16*1024*1024)throw std::runtime_error("ICC profile size outside bounds");f.seekg(0);valeo::Bytes data(static_cast<std::size_t>(n));if(!f.read(reinterpret_cast<char*>(data.data()),n))throw std::runtime_error("Cannot read ICC profile");valeo::validate_rgb_profile(data);return std::string("{\"ok\":true}");});}
extern "C" char* VCValidateProfileData(const void* data,unsigned long long size){return result([&]{if(!data||size<128||size>16*1024*1024)throw std::runtime_error("ICC profile size outside bounds");const auto bytes=static_cast<const std::uint8_t*>(data);valeo::validate_rgb_profile(valeo::Bytes(bytes,bytes+size));return std::string("{\"ok\":true}");});}
extern "C" char* VCWriteSyntheticDNG(const char* path){return result([&]{if(!path)throw std::runtime_error("No destination selected");valeo::write_dng(path,valeo::synthetic_metadata(),valeo::synthetic_mosaic());return std::string("{\"ok\":true,\"synthetic\":true}");});}
extern "C" void VCFree(void* p){std::free(p);}
