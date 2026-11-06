// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <deque>

namespace valeo {
struct ReplayStep { Command expected; Reply reply; };
class ReplayTransport final : public Transport {
    std::deque<ReplayStep> steps_;
    bool open_{};
    std::uint32_t generation_{1};
    std::uint32_t chunk_limit_{};
public:
    explicit ReplayTransport(std::deque<ReplayStep> steps,std::uint32_t chunk_limit=0x80000) : steps_(std::move(steps)),chunk_limit_(chunk_limit) {}
    std::vector<Device> enumerate() override;
    void open(std::uint64_t) override;
    Reply submit(const Command&) override;
    void close() noexcept override { open_=false; }
    std::uint32_t generation() const noexcept override { return generation_; }
    std::uint32_t frame_chunk_limit() const noexcept override{return chunk_limit_;}
    void reset(std::uint32_t g) noexcept { generation_=g;open_=false; }
    bool finished() const noexcept { return steps_.empty(); }
};
} // namespace valeo
