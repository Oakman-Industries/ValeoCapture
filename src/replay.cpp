// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/replay.hpp"

namespace valeo {
std::vector<Device> ReplayTransport::enumerate() { return {{1,1,0x6cd,0x609e,0x104d8,2,"replay://synthetic-candidate"}}; }
void ReplayTransport::open(std::uint64_t id) { if(id!=1)throw Error(ErrorCode::disconnected,"Unknown replay device");if(open_)throw Error(ErrorCode::busy,"Replay already open");open_=true; }
Reply ReplayTransport::submit(const Command& cmd) {
    cmd.validate();
    if (cmd.generation!=generation_) throw Error(ErrorCode::stale_generation,"Stale replay command");
    if (!open_) throw Error(ErrorCode::disconnected,"Replay closed");
    if (steps_.empty()) throw Error(ErrorCode::protocol,"Unexpected command after trace end");
    const auto& step=steps_.front(); const auto& e=step.expected;
    if(cmd.cdb!=e.cdb || cmd.direction!=e.direction || cmd.outgoing!=e.outgoing || cmd.incoming_size!=e.incoming_size || cmd.timeout!=e.timeout)
        throw Error(ErrorCode::protocol,"Command differs from replay evidence");
    auto result=step.reply;
    if(result.generation!=generation_ || result.sbp_status || result.response || result.data.size()!=cmd.incoming_size)
        throw Error(ErrorCode::protocol,"Replay status, generation or returned byte count is invalid");
    steps_.pop_front(); return result;
}
} // namespace valeo
