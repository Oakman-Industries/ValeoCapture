// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
// Reviewed reference command policy. Identity/model checks in the capture
// coordinator remain mandatory; a registry match alone never enables capture.
class ReferenceCommandGate {
    bool capture_{},parameter_pending_{},ready_{},fresh_idle_{},frame_announced_{};
    std::uint32_t remaining_{};
    std::uint32_t chunk_limit_{};
    bool auxiliary_enabled_{},auxiliary_{},triggered_{},light_drained_{},auxiliary_drained_{};
    std::uint32_t auxiliary_time_{};
    bool cached_auxiliary_{},reuse_permitted_{};
public:
    explicit ReferenceCommandGate(bool experimental_capture=false,std::uint32_t chunk_limit=0x80000,bool experimental_auxiliary=false);
    bool allows(const Command&) const;
    void completed(const Command&,const Bytes&);
    void reset() noexcept{parameter_pending_=false;ready_=false;fresh_idle_=false;frame_announced_=false;remaining_=0;auxiliary_=false;triggered_=false;light_drained_=false;auxiliary_drained_=false;auxiliary_time_=0;cached_auxiliary_=false;reuse_permitted_=false;}
};
} // namespace valeo
