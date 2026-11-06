// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/protocol.hpp"
#include "valeo/replay.hpp"
#include "valeo/journal.hpp"
#include "valeo/image_writer.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/raw_validation.hpp"
#ifdef VALEO_MACOS
#include "valeo/legacy_transport.hpp"
#include "valeo/host_transport.hpp"
#include "valeo/identity.hpp"
#endif
#include <iostream>
#include <iomanip>
#include <fstream>

int main(int argc,char** argv) {
    try {
        const std::string action=argc>1?argv[1]:"help";
        if(action=="list") {
#ifdef VALEO_MACOS
            const auto devices=valeo::discover_candidates();
            for(const auto& d:devices)std::cout<<"candidate registry_id="<<d.registry_id<<" guid=0x"<<std::hex<<d.guid<<std::dec<<" path="<<d.path<<"\n";
            std::cout<<devices.size()<<" Leaf SBP2 candidate(s); model identity and capture are not yet verified\n";
#else
            throw valeo::Error(valeo::ErrorCode::unsupported,"Native discovery requires pre-26 macOS");
#endif
        } else if(action=="probe" && argc==3) {
#ifdef VALEO_MACOS
            std::size_t parsed=0;const auto id=std::stoull(argv[2],&parsed);
            if(parsed!=std::string(argv[2]).size())throw std::runtime_error("Invalid registry ID");
            auto choice=valeo::host_transport(id);auto& transport=*choice.transport;transport.open(id);
            std::cout<<choice.backend<<" exclusive connection completed; generation="<<transport.generation()<<". No vendor command or image capture sent.\n";
            transport.close();
#else
            throw std::runtime_error("Native probe requires macOS");
#endif
        } else if(action=="identity" && argc==3) {
#ifdef VALEO_MACOS
            // Same reads as the app's Check Connection (VCInspectCamera): five
            // named identity parameters plus the binary firmware word. Read-only;
            // no arm, exposure or setting. The body serial is never printed.
            std::size_t parsed=0;const auto id=std::stoull(argv[2],&parsed);
            if(parsed!=std::string(argv[2]).size())throw std::runtime_error("Invalid registry ID");
            auto choice=valeo::host_transport(id);auto& transport=*choice.transport;transport.open(id);
            valeo::IdentityReader reader(transport);
            const auto identity=reader.identify();const auto firmware=reader.firmware_version();
            std::cout<<choice.backend<<" identity: type="<<identity.camera_type<<" revision="<<identity.revision
                     <<" sensor_size="<<identity.sensor_size<<" sensor_color="<<identity.sensor_color
                     <<" body_serial="<<(identity.body_serial.empty()?"absent":"present("+std::to_string(identity.body_serial.size())+" chars, not printed)")
                     <<" firmware="<<firmware<<" valeo11="<<(identity.is_valeo11()?"yes":"no")
                     <<" generation="<<transport.generation()<<"\n";
            transport.close();
#else
            throw std::runtime_error("Native identity requires macOS");
#endif
        } else if(action=="diagnostic-status" && argc==3) {
#ifdef VALEO_MACOS
            std::size_t parsed=0;const auto id=std::stoull(argv[2],&parsed);
            if(parsed!=std::string(argv[2]).size())throw std::runtime_error("Invalid registry ID");
            auto choice=valeo::host_transport(id);auto& transport=*choice.transport;transport.open(id);
            valeo::Command command;command.cdb=valeo::make_cdb(valeo::Direction::from_device,7,88);
            command.incoming_size=88;command.generation=transport.generation();command.timeout=std::chrono::milliseconds(35000);
            const auto reply=transport.submit(command);
            std::cout<<choice.backend<<" status (88 bytes; SBP2 backend has no residual count):\n";
            for(std::size_t i=0;i<reply.data.size();++i){std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(reply.data[i])<<((i%16==15)?'\n':' ');}
            std::cout<<"\nNo frame acknowledgement or setting sent. Keep output private until reviewed.\n";
#else
            throw std::runtime_error("Native diagnostics require macOS");
#endif
        } else if(action=="cdb" && argc==5) {
            const std::string dir=argv[2];if(dir!="in" && dir!="out")throw std::runtime_error("Use in or out");
            std::size_t a=0,b=0;auto op=std::stoul(argv[3],&a,0);auto size=std::stoul(argv[4],&b,0);
            if(a!=std::string(argv[3]).size() || b!=std::string(argv[4]).size() || op>255 || size>valeo::max_command_bytes)throw std::runtime_error("Invalid operation/length");
            for(auto byte:valeo::make_cdb(dir=="in"?valeo::Direction::from_device:valeo::Direction::to_device,static_cast<std::uint8_t>(op),static_cast<std::uint32_t>(size)))std::cout<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(byte)<<' ';
            std::cout<<"\nOffline encoding only; no device accessed.\n";
        } else if(action=="fingerprint-check" && argc==3) {
            // Offline: reads only the file's TIFF header and first IFD.
            if(valeo::has_provenance(argv[2])) {
                std::cout<<"Provenance mark present: "<<valeo::provenance_owner<<" / "<<valeo::provenance_payload<<'\n';
            } else {
                std::cout<<"No ValeoCapture provenance mark found\n";return 3;
            }
        } else if(action=="check-raw" && argc==3) {
            if(!std::filesystem::is_regular_file(argv[2]) || std::filesystem::file_size(argv[2])!=valeo::reference_frame_bytes)
                throw std::runtime_error("Expected a complete VA11A/5/C 21772608-byte BE16 wire frame");
            std::ifstream file(argv[2],std::ios::binary);if(!file)throw std::runtime_error("Cannot open raw frame");
            valeo::Raw14FrameValidator validator(valeo::reference_frame_bytes);
            while(file){valeo::Bytes chunk(0x80000);file.read(reinterpret_cast<char*>(chunk.data()),static_cast<std::streamsize>(chunk.size()));chunk.resize(static_cast<std::size_t>(file.gcount()));validator.append(chunk);}
            if(!file.eof())throw std::runtime_error("Raw file read failed");
            validator.finish();std::cout<<"Raw framing/range check passed: min="<<validator.minimum()<<" max="<<validator.maximum()<<". Calibration, color and exposure quality are not validated.\n";
        } else if(action=="demo-dng" && argc==3) {
            valeo::write_dng(argv[2],valeo::synthetic_metadata(),valeo::synthetic_mosaic());
            std::cout<<"Wrote synthetic 64x64 DNG fixture. This is not a camera capture.\n";
        } else {
            std::cout<<"valeoctl list | probe REGISTRY_ID | identity REGISTRY_ID | diagnostic-status REGISTRY_ID | cdb in|out OPERATION BYTE_COUNT | check-raw PRIVATE_VRAW | fingerprint-check DNG_OR_TIFF | demo-dng NEW_PATH\n"
                     <<"list is passive. probe opens an exclusive SBP2 login; close Leaf first.\n"
                     <<"diagnostic-status requires an explicit experimental build; normal builds reject its ORB.\n"
                     <<"Hardware capture is not enabled until protocol and lifecycle evidence is complete.\n";
            return action=="help"?0:2;
        }
        return 0;
    } catch(const std::exception& e) {std::cerr<<"Stopped: "<<e.what()<<'\n';return 1;}
}
