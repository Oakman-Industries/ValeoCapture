// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
enum class CompletionState { pending, success, timeout, reset, rejected };
// Copies transient callback data; never retains an IOKit message pointer.
// The token is opaque, monotonically increasing, and never dereferenced.
class Sbp2Completion {
    std::uint64_t token_{};
    std::uint32_t generation_{};
    CompletionState state_{CompletionState::pending};
    Bytes status_;
public:
    Sbp2Completion(std::uint64_t token, std::uint32_t generation);
    void notify(std::uint64_t token, std::uint32_t generation,
                std::uint32_t event, const std::uint8_t* status, std::size_t size);
    CompletionState state() const noexcept { return state_; }
    const Bytes& status() const noexcept { return status_; }
};
} // namespace valeo
