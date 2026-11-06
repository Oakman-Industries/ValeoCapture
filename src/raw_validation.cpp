// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/raw_validation.hpp"
#include <algorithm>

namespace valeo {
Raw14FrameValidator::Raw14FrameValidator(std::uint64_t expected):expected_(expected) {
    if(expected<2 || expected%2 || expected>512ull*1024*1024)
        throw Error(ErrorCode::invalid,"Invalid BE16 raw frame byte count");
}
void Raw14FrameValidator::append(const Bytes& bytes) {
    if(bytes.size()%2 || bytes.size()>expected_-received_)
        throw Error(ErrorCode::protocol,"Raw frame is unaligned or exceeds its expected size");
    for(std::size_t i=0;i<bytes.size();i+=2) {
        const auto value=static_cast<std::uint16_t>((static_cast<unsigned>(bytes[i])<<8)|bytes[i+1]);
        minimum_=std::min(minimum_,value);maximum_=std::max(maximum_,value);
        invalid_+=value>16383?1u:0u;
    }
    received_+=bytes.size();
}
void Raw14FrameValidator::finish() const {
    if(received_!=expected_)throw Error(ErrorCode::protocol,"Raw frame is incomplete");
    if(invalid_)throw Error(ErrorCode::protocol,"Raw frame contains out-of-range 14-bit samples; preserve it for diagnosis");
}
} // namespace valeo
