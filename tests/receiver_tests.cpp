// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/receiver.hpp"
#include "valeo/replay.hpp"
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace valeo;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
CaptureState armed(){CaptureState s;s.connected(1);std::string serial(46,'0');serial.replace(0,5,"VA11A");serial[45]='C';s.identified(serial);s.armed();return s;}
std::deque<ReplayStep> steps(){std::deque<ReplayStep> result;for(auto size:{0x80000u,2u}){Command c;c.cdb=make_cdb(Direction::from_device,0x14,size);c.incoming_size=size;c.generation=1;c.timeout=std::chrono::milliseconds(35000);result.push_back({c,Reply{Bytes(size,0x5a),1,0,0}});}return result;}
int main(){char temp[]="/tmp/valeo-receiver-XXXXXX";auto directory=::mkdtemp(temp);if(!directory)return 1;const std::filesystem::path base(directory);try{
    ReplayTransport replay(steps());replay.open(1);auto state=armed();std::uint64_t durable=0;
    auto receipt=receive_frame(replay,state,{1,0x80002},base/"opaque.vraw",[&](auto done,auto total){require(done<=total && done>durable,"Invalid progress");durable=done;});
    require(receipt.bytes==0x80002 && receipt.chunks==2 && replay.finished(),"Wrong Leaf chunking");
    std::deque<ReplayStep> small_steps;
    for(auto size:{61440u,2u}){Command c;c.cdb=make_cdb(Direction::from_device,0x14,size);c.incoming_size=size;c.generation=1;c.timeout=std::chrono::milliseconds(35000);small_steps.push_back({c,Reply{Bytes(size,0x2a),1,0,0}});}
    ReplayTransport small(std::move(small_steps),61440);small.open(1);auto smaller_state=armed();
    auto smaller_receipt=receive_frame(small,smaller_state,{1,61442},base/"smaller.vraw");
    require(smaller_receipt.bytes==61442 && smaller_receipt.chunks==2 && small.finished(),"Receiver ignored backend DMA ceiling");
    require(std::filesystem::file_size(base/"opaque.vraw")==receipt.bytes,"Wrong durable output size");
    std::ifstream f(base/"opaque.vraw",std::ios::binary);char value;std::uint64_t count=0;while(f.get(value)){require(value==0x5a,"Pixel transport corruption");++count;}require(count==receipt.bytes,"Truncated file");
    ReplayTransport cancelled(steps());cancelled.open(1);state=armed();bool stop=false;bool rejected=false;
    try{receive_frame(cancelled,state,{1,0x80002},base/"cancelled",[&](auto,auto){stop=true;},[&]{return stop;});}catch(const Error& e){rejected=e.code==ErrorCode::cancelled;}
    require(rejected && std::filesystem::file_size(base/"cancelled.partial")==0x80000 && !std::filesystem::exists(base/"cancelled"),"Cancellation lost or published partial data");
    ReplayTransport reset(steps());reset.open(1);state=armed();rejected=false;
    try{receive_frame(reset,state,{1,0x80002},base/"reset",[&](auto,auto){reset.reset(2);});}catch(const Error& e){rejected=e.code==ErrorCode::stale_generation;}
    require(rejected && state.state()==SessionState::faulted && std::filesystem::file_size(base/"reset.partial")==0x80000,"Reset reused an old capture");
    auto short_steps=steps();short_steps[0].reply.data.pop_back();ReplayTransport short_read(std::move(short_steps));short_read.open(1);state=armed();rejected=false;
    try{receive_frame(short_read,state,{1,0x80002},base/"short");}catch(const Error&){rejected=true;}require(rejected && !std::filesystem::exists(base/"short"),"Short read published");
    std::filesystem::remove_all(base);std::cout<<"PASS replay receiver: 512 KiB chunk boundary, exact bytes, durable progress, cancellation, reset, short transfer\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';std::filesystem::remove_all(base);return 1;}}
