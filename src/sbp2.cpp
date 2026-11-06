// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/sbp2.hpp"
#include <algorithm>

namespace valeo {
Sbp2Completion::Sbp2Completion(std::uint64_t token, std::uint32_t generation)
    : token_(token), generation_(generation) {
    if(!token)throw Error(ErrorCode::invalid,"SBP2 completion token cannot be zero");
}
void Sbp2Completion::notify(std::uint64_t token, std::uint32_t generation,
                            std::uint32_t event, const std::uint8_t* status, std::size_t size) {
    if(state_!=CompletionState::pending || token!=token_)return; // old or duplicate callback
    if(generation!=generation_ || event==9){state_=CompletionState::reset;return;}
    if(event==7){state_=CompletionState::timeout;return;}
    if(event!=6 || !status || size<8 || size>32){state_=CompletionState::rejected;return;}
    status_.assign(status,status+size);
    // SBP2: response bits 5:4, dead bit 3, length in quadlets minus one.
    // Until observed traffic establishes its extension, accept only zero extended
    // status. Unknown extensions are failures, never successful image transfer.
    if((status_[0]&0x38) || status_[1] || ((status_[0]&7)+1u)*4u!=size ||
       std::any_of(status_.begin()+8,status_.end(),[](std::uint8_t b){return b!=0;})) {
        state_=CompletionState::rejected;return;
    }
    state_=CompletionState::success;
}
} // namespace valeo
