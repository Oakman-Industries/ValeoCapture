// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_export.hpp"
#include "valeo/raw_pipeline.hpp"
#include "valeo/color.hpp"
#include "valeo/journal.hpp"
#include "valeo/capture_config.hpp"
#include <pthread/qos.h>
#include <sstream>
#include <iomanip>
#include <locale>
#include <cmath>

namespace valeo {
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
}
CaptureExporter::CaptureExporter(Bytes icc,bool tiff):icc_(std::move(icc)),gamma_(rgb_profile_gamma(icc_)),tiff_(tiff){}
CaptureExporter::~CaptureExporter(){if(result_.valid())result_.wait();}
bool CaptureExporter::ready()const{return result_.valid()&&result_.wait_for(std::chrono::seconds(0))==std::future_status::ready;}
const char* CaptureExporter::phase()const {
    switch(stage_.load()) {
        case 1:return "Correcting sensor data";
        case 2:return "Saving editable DNG";
        case 3:return "Developing TIFF";
        case 4:return "Applying Leaf colour preset";
        case 5:return "Saving 16-bit TIFF";
        case 6:return "Files saved";
        default:return "";
    }
}
CaptureExportResult CaptureExporter::take(){if(!busy())throw Error(ErrorCode::invalid,"No export is pending");return result_.get();}
void CaptureExporter::submit(CapturedPair pair) {
    if(busy())throw Error(ErrorCode::busy,"Finish the existing export before queuing another pair");
    // Snapshot all rendering inputs before creating the worker. Device storage
    // was already durably committed by the sole transport owner.
    stage_=1;
    result_=std::async(std::launch::async,[this,pair=std::move(pair)]() mutable {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED,0);
        auto tick=Clock::now();const auto mosaic=subtract_valeo11_pair(pair.light,pair.dark);
        pair.light=Bytes{};pair.dark=Bytes{};
        const auto correction=elapsed(tick);stage_=2;tick=Clock::now();
        CaptureExportResult result;write_dng(pair.stem+".dng",pair.metadata,mosaic);result.dng=pair.stem+".dng";
        const auto dng=elapsed(tick);double demosaic=0,icc=0,tiff=0;
        if(tiff_) {
            stage_=3;tick=Clock::now();const auto rgb=demosaic_camera_rgb(mosaic,pair.metadata,gamma_);
            demosaic=elapsed(tick);stage_=4;tick=Clock::now();
            const auto rendered=render_profiled_rgb(pair.metadata.width,pair.metadata.height,rgb,icc_);
            icc=elapsed(tick);stage_=5;tick=Clock::now();
            write_rgb_tiff(pair.stem+".tiff",pair.metadata.width,pair.metadata.height,pair.metadata.orientation,rendered.pixels,rendered.output_icc);
            tiff=elapsed(tick);result.tiff=pair.stem+".tiff";
        }
        const auto total=elapsed(pair.began);
        std::ostringstream metrics;metrics.imbue(std::locale::classic());metrics<<std::fixed<<std::setprecision(6)
            <<"{\"schema\":2,\"iso\":"<<pair.metadata.iso<<",\"isoMode\":\"software\",\"softwareGain\":"<<std::exp2(pair.metadata.baseline_exposure_ev)
            <<",\"baselineExposureEV\":"<<pair.metadata.baseline_exposure_ev<<",\"cameraISOCommand\":25,\"analogueGainChanged\":false"
            <<",\"bytesPerPlane\":"<<reference_frame_bytes<<",\"readySeconds\":"<<pair.ready_seconds
            <<",\"calibrationReused\":"<<(pair.calibration_reused?"true":"false")<<",\"calibrationSource\":\""<<pair.calibration_source<<"\""
            <<",\"photoReadSeconds\":"<<pair.photo_read<<",\"photoJournalSeconds\":"<<pair.photo_journal
            <<",\"darkWaitSeconds\":"<<pair.dark_wait<<",\"darkReadSeconds\":"<<pair.dark_read<<",\"darkJournalSeconds\":"<<pair.dark_journal
            <<",\"photoPrepareSeconds\":"<<pair.photo_transport[0]<<",\"photoExecuteSeconds\":"<<pair.photo_transport[1]<<",\"photoCompletionSeconds\":"<<pair.photo_transport[2]
            <<",\"darkPrepareSeconds\":"<<pair.dark_transport[0]<<",\"darkExecuteSeconds\":"<<pair.dark_transport[1]<<",\"darkCompletionSeconds\":"<<pair.dark_transport[2]
            <<",\"correctionSeconds\":"<<correction<<",\"dngSeconds\":"<<dng<<",\"demosaicSeconds\":"<<demosaic
            <<",\"iccSeconds\":"<<icc<<",\"tiffSeconds\":"<<tiff<<",\"totalSeconds\":"<<total<<"}\n";
        const auto text=metrics.str();Journal journal(pair.stem+".timing.json",text.size());journal.append(Bytes(text.begin(),text.end()));journal.commit();
        std::ostringstream summary;summary.imbue(std::locale::classic());summary<<std::fixed<<std::setprecision(1)
            <<"Ready in "<<pair.ready_seconds<<" s · files saved in "<<total<<" s · conversion "<<correction+dng+demosaic+icc+tiff<<" s";
        result.timing=summary.str();stage_=6;return result;
    });
}
} // namespace valeo
