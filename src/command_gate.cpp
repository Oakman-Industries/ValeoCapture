// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/command_gate.hpp"
#include "valeo/identity.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/protocol.hpp"
#include <algorithm>

namespace valeo {
ReferenceCommandGate::ReferenceCommandGate(bool capture,std::uint32_t limit,bool auxiliary):capture_(capture),chunk_limit_(limit),auxiliary_enabled_(auxiliary) {
    if(limit<2 || limit>0x80000 || limit%2)throw Error(ErrorCode::invalid,"Invalid backend frame chunk ceiling");
    if(auxiliary && !capture)throw Error(ErrorCode::invalid,"Auxiliary policy requires capture");
}
bool ReferenceCommandGate::allows(const Command& c) const {
    c.validate();
    const bool inbound=c.direction==Direction::from_device;
    const bool response=inbound && c.cdb[1]==0 && c.incoming_size==64 && parameter_pending_;
    if(parameter_pending_)return response;
    if(is_parameter_selector(c))return !remaining_;
    if(inbound && ((c.cdb[1]==7 && c.incoming_size==88) || (c.cdb[1]==0x22 && c.incoming_size==480)))return true;
    if(!capture_)return false;
    if(is_reference_iso(c))return !auxiliary_ && ((!remaining_ && !light_drained_ && !auxiliary_drained_) ||
        (fresh_idle_ && remaining_==reference_frame_bytes && !ready_));
    if(auxiliary_enabled_) {
        if(is_reference_auxiliary_arm(c))return light_drained_ && !remaining_ && !auxiliary_;
        if(is_reference_auxiliary_trigger(c))return auxiliary_ && !triggered_ && remaining_==reference_frame_bytes;
        const auto rearm=timed_light_rearm(c.generation,auxiliary_time_);
        if(auxiliary_drained_ && c.direction==rearm.direction && c.cdb==rearm.cdb && c.outgoing==rearm.outgoing && !c.incoming_size)return true;
    }
    if(is_reference_physical_arm(c))return !remaining_ && !auxiliary_;
    if(is_reference_physical_setup(c))return !ready_ && !auxiliary_;
    return inbound && c.cdb[1]==0x14 && ready_ && c.incoming_size>=2 &&
        c.incoming_size==std::min(chunk_limit_,remaining_);
}
void ReferenceCommandGate::completed(const Command& c,const Bytes& data) {
    if(!allows(c))throw Error(ErrorCode::protocol,"Completion outside reviewed command sequence");
    const bool inbound=c.direction==Direction::from_device;
    if(data.size()!=(inbound?c.incoming_size:0))throw Error(ErrorCode::protocol,"Completion length disagrees with command");
    parameter_pending_=is_parameter_selector(c);
    // Observed traffic changes ISO between zero-byte status polls while the
    // light arm remains active. A fresh idle poll is required for every change;
    // never interrupt a ready/partly-drained frame or the auxiliary sequence.
    fresh_idle_=capture_ && inbound && c.cdb[1]==7 && remaining_==reference_frame_bytes &&
        !auxiliary_ && !frame_announced_ && read_be32(data,0)==0;
    // Recover a frame already pending when a new, identified coordinator opens.
    // Default diagnostics never gain frame-read permission from a status reply.
    if(capture_ && inbound && c.cdb[1]==7 && !remaining_ && (!light_drained_ || (auxiliary_enabled_ && cached_auxiliary_ && reuse_permitted_)) && !auxiliary_drained_ && read_be32(data,0)==reference_frame_bytes) {
        remaining_=reference_frame_bytes;ready_=true;frame_announced_=true;light_drained_=false;reuse_permitted_=false;
    }
    if(!inbound && c.cdb[1]==0x12){
        auxiliary_=is_reference_auxiliary_arm(c);remaining_=reference_frame_bytes;ready_=false;
        triggered_=false;light_drained_=false;auxiliary_drained_=false;auxiliary_time_=0;
        reuse_permitted_=false;
        frame_announced_=false;
    }
    if(is_reference_auxiliary_trigger(c))triggered_=true;
    // Observed traffic branches to auxiliary acquisition on CP word9 != 0;
    // otherwise it continues polling without issuing another light arm.
    if(inbound && c.cdb[1]==0x22 && !auxiliary_ && ready_ && remaining_==reference_frame_bytes)
        reuse_permitted_=read_be32(data,36)==0;
    if(inbound && c.cdb[1]==7 && remaining_==reference_frame_bytes) {
        ready_=(!auxiliary_ || triggered_) && read_be32(data,0)==reference_frame_bytes;
        if(ready_)frame_announced_=true;
        if(ready_ && auxiliary_)auxiliary_time_=read_be32(data,0x38);
    }
    if(inbound && c.cdb[1]==0x14){
        remaining_-=c.incoming_size;
        if(!remaining_){ready_=false;light_drained_=!auxiliary_;auxiliary_drained_=auxiliary_;if(auxiliary_)cached_auxiliary_=true;}
    }
}
} // namespace valeo
