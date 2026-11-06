// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <memory>

namespace valeo {
struct HostTransportChoice {
    std::unique_ptr<Transport> transport;
    const char* backend;
};
// Choose from passive registry evidence before opening. A failure to obtain the
// selected backend's ownership is terminal; never bypass it by trying another.
HostTransportChoice host_transport(std::uint64_t lun_registry_id);
// Passive discovery of exact Leaf candidates for the running OS. macOS 26+:
// ASFW SBP-2 units that already publish a SCSITaskLib logical unit (IDs are
// the ASFWSBP2Nub registry entries). Earlier systems: Apple IOFireWireSBP2LUN.
std::vector<Device> discover_candidates();
} // namespace valeo
