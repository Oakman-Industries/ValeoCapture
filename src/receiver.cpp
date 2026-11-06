// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/receiver.hpp"
#include "valeo/journal.hpp"
#include <algorithm>

namespace valeo {
ReceiveReceipt receive_frame(Transport& transport,CaptureState& state,const ReceivePlan& plan,
    const std::filesystem::path& destination,
    const std::function<void(std::uint64_t,std::uint64_t)>& progress,
    const std::function<bool()>& cancelled) {
    if(!plan.frame_bytes || plan.frame_bytes>512ull*1024*1024 || (plan.frame_bytes&1) ||
       plan.chunk_timeout.count()<1 || plan.chunk_timeout.count()>120000)
        throw Error(ErrorCode::invalid,"Frame size/timeout outside bounds");
    if(state.state()!=SessionState::armed)throw Error(ErrorCode::invalid,"Receive requires an identified, armed session");
    const auto limit=transport.frame_chunk_limit();
    if(limit<2 || limit>0x80000 || limit%2)throw Error(ErrorCode::invalid,"Backend frame chunk limit outside bounds");
    state.receiving(plan.generation);
    try {
        Journal journal(destination,plan.frame_bytes);
        ReceiveReceipt receipt{0,0,plan.generation};
        while(receipt.bytes<plan.frame_bytes) {
            if(cancelled && cancelled())throw Error(ErrorCode::cancelled,"Capture cancelled; received bytes retained in .partial");
            if(transport.generation()!=plan.generation)throw Error(ErrorCode::stale_generation,"Bus reset before next frame chunk");
            const auto size=static_cast<std::uint32_t>(std::min<std::uint64_t>(limit,plan.frame_bytes-receipt.bytes));
            Command c;c.cdb=make_cdb(Direction::from_device,0x14,size);c.incoming_size=size;
            c.generation=plan.generation;c.timeout=plan.chunk_timeout;
            const auto reply=transport.submit(c);
            if(reply.generation!=plan.generation || transport.generation()!=plan.generation)
                throw Error(ErrorCode::stale_generation,"Bus reset during frame chunk");
            if(reply.sbp_status || reply.response || reply.data.size()!=size)
                throw Error(ErrorCode::protocol,"Failed or short frame chunk");
            journal.append(reply.data);receipt.bytes+=size;++receipt.chunks;
            if(progress)progress(receipt.bytes,plan.frame_bytes);
        }
        if(cancelled && cancelled())throw Error(ErrorCode::cancelled,"Capture cancelled before publication; complete bytes retained in .partial");
        if(transport.generation()!=plan.generation)throw Error(ErrorCode::stale_generation,"Bus reset before frame publication");
        state.committing();journal.commit();state.committed();return receipt;
    } catch(...) {state.reset(transport.generation());throw;}
}
} // namespace valeo
