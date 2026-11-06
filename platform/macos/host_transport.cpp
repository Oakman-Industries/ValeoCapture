// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/host_transport.hpp"
#include "valeo/legacy_transport.hpp"
#include "valeo/scsi_transport.hpp"
#include <algorithm>

namespace valeo {
std::vector<Device> discover_candidates() {
    if(__builtin_available(macOS 26.0,*))return ScsiTransport{}.enumerate();
    return LegacyTransport{}.enumerate();
}
HostTransportChoice host_transport(std::uint64_t id) {
    if(__builtin_available(macOS 26.0,*)) {
        // Apple's FireWire stack is absent; the only reviewed route is
        // SCSITaskLib on the logical unit ASFW publishes. No fallback.
        auto scsi=std::make_unique<ScsiTransport>();
        const auto units=scsi->enumerate();
        if(std::none_of(units.begin(),units.end(),[&](const Device& d){return d.registry_id==id && d.leaf_candidate();}))
            throw Error(ErrorCode::disconnected,"Leaf candidate disappeared; refresh discovery");
        return {std::move(scsi),"ASFW SCSI"};
    }
    auto legacy=std::make_unique<LegacyTransport>();
    const auto luns=legacy->enumerate();
    if(std::none_of(luns.begin(),luns.end(),[&](const Device& d){return d.registry_id==id && d.leaf_candidate();}))
        throw Error(ErrorCode::disconnected,"Leaf candidate disappeared; refresh discovery");
    auto scsi=std::make_unique<ScsiTransport>();
    const auto peripherals=scsi->enumerate();
    if(std::any_of(peripherals.begin(),peripherals.end(),[&](const Device& d){return d.registry_id==id;}))
        return {std::move(scsi),"Apple SCSI"};
    return {std::move(legacy),"Apple SBP2"};
}
} // namespace valeo
