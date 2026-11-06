// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <memory>

namespace valeo {
// Thread-confined: use from one run-loop thread, including destruction.
// Default build: login only. Experimental reads allowlist fixed diagnostics and
// named identity query pairs. A separate capture experiment permits only the
// observed VA11A physical setup/arm and status-gated raw chunks. Not a release API.
class LegacyTransport final : public Transport {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    LegacyTransport();
    ~LegacyTransport() override;
    std::vector<Device> enumerate() override;
    void open(std::uint64_t registry_id) override;
    Reply submit(const Command&) override;
    void close() noexcept override;
    std::uint32_t generation() const noexcept override;
};
} // namespace valeo
