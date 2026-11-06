// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <optional>

namespace valeo {
struct CachedCalibration {Bytes pixels,status,parameters;std::string source;std::uint32_t generation{};};
// VA11A/5/C fw3301 reference: capture-parameter BE32 word9 requests an auxiliary
// when nonzero. A clear flag permits reuse only after our own validated fresh
// auxiliary in this session. Never import the camera's unowned earlier cache.
class CalibrationCache {
    std::optional<CachedCalibration> frame_;
public:
    void remember(CachedCalibration);
    bool can_reuse(std::uint32_t generation,const Bytes& light_status,const Bytes& parameters)const;
    const CachedCalibration& current()const;
};
} // namespace valeo
