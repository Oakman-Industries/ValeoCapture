// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"

namespace valeo {
// Diagnostic guard for the observed VA11A/5/C BE16, 14-bit wire plane. This
// checks framing/range, not CFA, calibration, exposure quality or color accuracy.
// Invalid sample counts accumulate while the complete frame is drained/saved.
class Raw14FrameValidator {
    std::uint64_t expected_{},received_{},invalid_{};
    std::uint16_t minimum_{65535},maximum_{};
public:
    explicit Raw14FrameValidator(std::uint64_t expected_bytes);
    void append(const Bytes&);
    void finish() const;
    std::uint64_t received() const noexcept{return received_;}
    std::uint64_t invalid_samples() const noexcept{return invalid_;}
    std::uint16_t minimum() const noexcept{return minimum_;}
    std::uint16_t maximum() const noexcept{return maximum_;}
};
} // namespace valeo
