// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/calibration_cache.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/protocol.hpp"
#include "valeo/raw_validation.hpp"
#include <algorithm>

namespace valeo {
namespace {
bool matching_metadata(const Bytes& status,const Bytes& parameters) {
    return status.size()==88 && parameters.size()==480 && read_be32(status,0)==reference_frame_bytes &&
        read_be32(status,0x38)!=0 && read_be32(status,0x38)==read_be32(parameters,44);
}
}
void CalibrationCache::remember(CachedCalibration frame) {
    if(frame.pixels.size()!=reference_frame_bytes || !matching_metadata(frame.status,frame.parameters) ||
       frame.source.empty() || frame.source.size()>255 ||
       !std::all_of(frame.source.begin(),frame.source.end(),[](unsigned char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.';}))
        throw Error(ErrorCode::invalid,"Cannot cache incomplete or inconsistent calibration");
    Raw14FrameValidator check(reference_frame_bytes);check.append(frame.pixels);check.finish();
    frame_=std::move(frame);
}
bool CalibrationCache::can_reuse(std::uint32_t generation,const Bytes& status,const Bytes& parameters)const {
    return frame_ && frame_->generation==generation && matching_metadata(status,parameters) && read_be32(parameters,36)==0;
}
const CachedCalibration& CalibrationCache::current()const {
    if(!frame_)throw Error(ErrorCode::invalid,"No calibration owned by this session");return *frame_;
}
} // namespace valeo
