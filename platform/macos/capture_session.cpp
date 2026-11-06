// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_session.hpp"
#include "valeo/capture_export.hpp"
#include "valeo/calibration_cache.hpp"
#include "valeo/host_transport.hpp"
#include "valeo/identity.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/protocol.hpp"
#include "valeo/journal.hpp"
#include "valeo/raw_validation.hpp"
#include "valeo/raw_pipeline.hpp"
#include "valeo/color.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <chrono>
#include <memory>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>

namespace valeo {
namespace {
Command read_command(std::uint8_t op,std::uint32_t count,std::uint32_t generation) {
    Command c;c.cdb=make_cdb(Direction::from_device,op,count);c.incoming_size=count;c.generation=generation;
    c.timeout=std::chrono::milliseconds(op==0x22?3000:35000);return c;
}
std::filesystem::path new_stem(const std::filesystem::path& folder) {
    const auto stamp=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return folder/("Valeo11-"+std::to_string(stamp));
}
void save_sidecar(const std::filesystem::path& path,const Bytes& data) {Journal out(path,data.size());out.append(data);out.commit();}
void wait_briefly(double seconds){CFRunLoopRunInMode(kCFRunLoopDefaultMode,seconds,false);}
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
struct PlaneTiming {double read{},journal{};std::array<double,3> transport{};};
} // namespace
void run_capture_session(const CaptureSettings& settings,const std::atomic_bool& stop,const std::atomic_uint& requested_iso,const std::function<void(const CaptureProgress&)>& report) {
    CaptureProgress p;
    CaptureExporter exporter(settings.input_icc,settings.save_tiff);
    CalibrationCache calibration;
    const auto publish=[&]{p.export_pending=exporter.busy();p.export_phase=exporter.busy()?exporter.phase():"";report(p);};
    const auto update=[&](const char* phase){p.phase=phase;publish();};
    const auto collect_export=[&](bool wait){
        if(exporter.busy() && (wait || exporter.ready())) {
            const auto files=exporter.take();p.last_dng=files.dng;p.last_tiff=files.tiff;p.timing=files.timing;++p.completed;publish();
        }
    };
    auto metadata=settings.camera;
    // Apple's native Leaf decoder overrides BaselineExposure (even valid +3EV)
    // with a camera default. Identify our converted data so the generic DNG
    // decoder uses its explicit matrix/exposure; retain physical make/model.
    metadata.unique_camera_model="ValeoCapture Leaf Valeo 11";
    validate_raw_metadata(metadata);
    if(metadata.width!=4008 || metadata.height!=2672 || metadata.cfa!=std::array<std::uint8_t,4>{2,1,1,0} || metadata.black_level!=0 ||
       settings.expected_body_serial.empty() || settings.expected_body_serial.size()>64 || !std::filesystem::is_directory(settings.folder))
        throw Error(ErrorCode::invalid,"A matching Valeo11 calibration and an existing capture folder are required");
    valeo11_software_exposure_stops(settings.software_iso); // Host setting only.
    auto choice=host_transport(settings.registry_id);auto& t=*choice.transport;
    if(!t.paired_capture_enabled())throw Error(ErrorCode::unsupported,"This backend build does not enable paired capture");
    p.stage="connecting";update("Connecting to the back");t.open(settings.registry_id);
    auto identify=[&]{
        IdentityReader reader(t);const auto identity=reader.identify();
        if(identity.revision!="A" || identity.sensor_size!="5" || identity.sensor_color!="C" || reader.firmware_version()!=3301)
            throw Error(ErrorCode::unsupported,"Capture is currently verified for VA11A/5/C firmware3301");
        if(identity.body_serial!=settings.expected_body_serial)throw Error(ErrorCode::unsupported,"Import calibration for this back before capturing");
        metadata.serial=identity.body_serial;
    };
    identify();auto generation=t.generation();
    const auto status=[&]{return t.submit(read_command(7,88,generation)).data;};
    const auto ensure_generation=[&]{if(t.generation()!=generation)throw Error(ErrorCode::stale_generation,"Camera bus changed; reconnect and identify again");};
    const auto receive=[&](Journal& journal,const char* phase,double base,double span,std::uint64_t offset,PlaneTiming& timing) {
        p.received=offset;p.fraction=base;update(phase);
        Raw14FrameValidator validator(reference_frame_bytes);Bytes plane;plane.reserve(reference_frame_bytes);
        std::uint32_t remaining=reference_frame_bytes,chunks=0;
        while(remaining) {
            const auto size=std::min(remaining,t.frame_chunk_limit());
            auto tick=Clock::now();
            const auto reply=t.submit(read_command(0x14,size,generation));
            timing.read+=elapsed(tick);
            timing.transport[0]+=reply.prepare_seconds;timing.transport[1]+=reply.execute_seconds;timing.transport[2]+=reply.completion_seconds;
            if(reply.data.size()!=size)throw Error(ErrorCode::protocol,"Camera returned a short image chunk");
            tick=Clock::now();journal.append(reply.data);timing.journal+=elapsed(tick);
            validator.append(reply.data);plane.insert(plane.end(),reply.data.begin(),reply.data.end());
            remaining-=size;p.received+=size;p.fraction=base+span*(reference_frame_bytes-remaining)/reference_frame_bytes;
            if(++chunks%8==0)publish();
        }
        validator.finish();ensure_generation();const auto tick=Clock::now();journal.commit();timing.journal+=elapsed(tick);publish();return plane;
    };
    auto initial=status();
    if(read_be32(initial,0)) {
        if(read_be32(initial,0)!=reference_frame_bytes)throw Error(ErrorCode::protocol,"Unknown pending camera frame size; nothing overwritten");
        const auto preserved=new_stem(settings.folder).string()+"-pending.vraw";
        Journal journal(preserved,reference_frame_bytes);save_sidecar(preserved+".ready-status",initial);
        save_sidecar(preserved+".capture-parameters",t.submit(read_command(0x22,480,generation)).data);
        PlaneTiming timing;p.total=reference_frame_bytes;p.stage="preserving";
        receive(journal,"Preserving an earlier pending frame",0,1,0,timing);p.last_raw=preserved;
        if(read_be32(status(),0))throw Error(ErrorCode::busy,"Camera has another pending frame; preserved raw and stopped");
        // Start a fresh configuration policy after preserving an unknown plane.
        t.close();t.open(settings.registry_id);identify();generation=t.generation();
        if(read_be32(status(),0))throw Error(ErrorCode::busy,"Camera has a pending frame after reconnect; preserved raw and stopped");
    }
    if(stop){update("stopped");return;}
    t.submit(physical_setup(PhysicalSetup::release_interface,generation));
    // Keep the camera-side/body-meter ISO command at the verified base value.
    // Software gain changes never issue a camera or analogue-gain command.
    t.submit(select_verified_iso(25,generation));
    p.software_iso=settings.software_iso;
    t.submit(physical_arm(generation));t.submit(physical_setup(PhysicalSetup::rapid_mode,generation));
    t.submit(physical_setup(PhysicalSetup::ready_beep,generation));wait_briefly(0.151);
    try { while(!stop) {
        collect_export(false);
        p.received=0;p.total=0;p.fraction=p.last_raw.empty()?0:1;p.stage="waiting";update("Ready for the shutter");
        Bytes light_status;
        while(!stop) {
            collect_export(false);
            const auto requested=requested_iso.load();
            if(requested>65535)throw Error(ErrorCode::invalid,"Software ISO outside bounds");
            valeo11_software_exposure_stops(static_cast<std::uint16_t>(requested));
            if(requested!=p.software_iso){p.software_iso=static_cast<std::uint16_t>(requested);publish();}
            const auto reply=status();const auto size=read_be32(reply,0);
            if(size==reference_frame_bytes){light_status=reply;break;}
            if(size)throw Error(ErrorCode::protocol,"Camera announced an unknown image size");
            publish();wait_briefly(0.1);
        }
        if(light_status.empty())break;
        // Freeze per-image metadata. A software-gain request received during
        // either plane or export affects only the next image.
        metadata.iso=p.software_iso;p.frame_iso=p.software_iso;
        metadata.baseline_exposure_ev=valeo11_software_exposure_stops(p.frame_iso);
        const auto capture_begin=Clock::now();
        PlaneTiming photo_time,dark_time;p.total=std::uint64_t(reference_frame_bytes)*2;
        const auto stem=new_stem(settings.folder).string();
        const auto light_path=stem+".vraw",dark_path=stem+".auxiliary.vraw";
        Journal light_file(light_path,reference_frame_bytes),dark_file(dark_path,reference_frame_bytes);
        save_sidecar(light_path+".ready-status",light_status);
        const auto light_parameters=t.submit(read_command(0x22,480,generation)).data;
        save_sidecar(light_path+".capture-parameters",light_parameters);
        const bool reused=settings.reuse_calibration && calibration.can_reuse(generation,light_status,light_parameters);
        p.calibration_reused=reused;p.total=std::uint64_t(reference_frame_bytes)*(reused?1:2);
        p.stage="photograph";
        auto light=receive(light_file,"Receiving photograph",0,reused?1:0.35,0,photo_time);p.last_raw=light_path;
        if(read_be32(status(),0))throw Error(ErrorCode::protocol,"Image saved but camera has pending bytes; refusing auxiliary arm");
        double dark_wait=0;Bytes dark,dark_status,dark_parameters;
        if(reused) {
            p.stage="calibration";update("Using camera-approved calibration");
            const auto& cached=calibration.current();dark=cached.pixels;dark_status=cached.status;dark_parameters=cached.parameters;
            auto tick=Clock::now();dark_file.append(dark);dark_file.commit();
            save_sidecar(dark_path+".ready-status",dark_status);save_sidecar(dark_path+".capture-parameters",dark_parameters);
            dark_time.journal=elapsed(tick);
            // Reference skips auxiliary arm/trigger AND light rearm for this
            // branch. Rapid mode is already waiting for the next physical shot.
        } else {
        const auto auxiliary_begin=Clock::now();
        p.stage="calibration";
        t.submit(auxiliary_arm(generation));t.submit(auxiliary_trigger(generation));update("Preparing fresh calibration");
        const auto deadline=CFAbsoluteTimeGetCurrent()+60;
        while(CFAbsoluteTimeGetCurrent()<deadline) {
            const auto reply=status();const auto size=read_be32(reply,0);
            if(size==reference_frame_bytes){dark_status=reply;break;}
            if(size)throw Error(ErrorCode::protocol,"Camera announced an unknown auxiliary size");
            wait_briefly(0.01);
        }
        if(dark_status.empty())throw Error(ErrorCode::timeout,"Auxiliary timed out; light raw remains saved");
        dark_wait=elapsed(auxiliary_begin);
        save_sidecar(dark_path+".ready-status",dark_status);
        dark=receive(dark_file,"Receiving fresh calibration",0.45,0.35,reference_frame_bytes,dark_time);
        dark_parameters=t.submit(read_command(0x22,480,generation)).data;
        save_sidecar(dark_path+".capture-parameters",dark_parameters);
        if(read_be32(status(),0))throw Error(ErrorCode::protocol,"Auxiliary saved but camera still has pending bytes");
        t.submit(timed_light_rearm(generation,read_be32(dark_status,0x38)));
        t.submit(physical_setup(PhysicalSetup::ready_beep,generation));wait_briefly(0.151);
        if(!read_be32(light_status,0x38) || read_be32(light_status,0x38)!=read_be32(dark_status,0x38))
            throw Error(ErrorCode::protocol,"Light/auxiliary timing differs; original planes preserved without conversion");
        calibration.remember({dark,dark_status,dark_parameters,std::filesystem::path(dark_path).filename().string(),generation});
        }
        // One exporting pair plus one receiver is the strict memory bound. If
        // storage is slower than capture, finish the earlier export before
        // advertising readiness; never discard or grow an unbounded queue.
        if(exporter.busy()) {p.stage="finishing";update("Finishing the previous image");collect_export(true);}
        CapturedPair pair;pair.light=std::move(light);pair.dark=std::move(dark);
        pair.metadata=metadata;pair.stem=stem;pair.began=capture_begin;
        pair.ready_seconds=elapsed(capture_begin);pair.photo_read=photo_time.read;pair.photo_journal=photo_time.journal;
        pair.dark_wait=dark_wait;pair.dark_read=dark_time.read;pair.dark_journal=dark_time.journal;
        pair.photo_transport=photo_time.transport;pair.dark_transport=dark_time.transport;
        pair.calibration_reused=reused;pair.calibration_source=calibration.current().source;
        exporter.submit(std::move(pair));
    }} catch(...) {
        // Close on the original owner before waiting for CPU-only work. A bus
        // failure must not abandon a previously committed pair's export.
        t.close();try{collect_export(true);}catch(...){}throw;
    }
    t.close();
    if(exporter.busy()){p.stage="finishing";update("Finishing file export");collect_export(true);}

    p.stage="stopped";update("Stopped");
}
} // namespace valeo
