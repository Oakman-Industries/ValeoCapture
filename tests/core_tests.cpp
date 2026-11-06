// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/protocol.hpp"
#include "valeo/replay.hpp"
#include "valeo/journal.hpp"
#include <functional>
#include <fstream>
#include <iostream>
#include <unistd.h>

using namespace valeo;
static int failures=0;
static void expect(bool b,const char* why){if(!b){std::cerr<<"FAIL "<<why<<'\n';++failures;}}
static void rejects(const std::function<void()>& f,const char* why){try{f();expect(false,why);}catch(const Error&){} }
int main(){
    expect(make_cdb(Direction::from_device,7,88)==std::array<std::uint8_t,6>{0x28,7,0,0,88,0},"vendor CDB fixed vector");
    expect(make_cdb(Direction::to_device,0x20,0x123456)==std::array<std::uint8_t,6>{0x0f,0x20,0x12,0x34,0x56,0},"24-bit length byte order");
    rejects([]{make_cdb(Direction::from_device,0,0x1000000);},"length overflow");
    rejects([]{make_cdb(Direction::to_device,0,0);},"undefined zero transfer");
    Device d{1,2,0x6cd,0x609e,0x104d8,2,"synthetic"};expect(d.leaf_candidate(),"tuple match");d.device_type=0;expect(!d.leaf_candidate(),"mass storage excluded");
    expect(!valeo11_family("VA11"),"bounded serial prefix");
    std::string serial(46,'0');serial.replace(0,5,"VA11A");serial[45]='C';expect(valid_valeo11_serial(serial),"reference serial classifier");serial[44]='x';expect(!valid_valeo11_serial(serial),"serial numeric field");
    Bytes four{0,0,0,8,0xff,0xd8,0xff,0xf7};expect(parse_jpegls_envelope(four,8).prefix_size==4,"legacy prefix");
    Bytes eight{0,0,0,0,0,0,0,8,0xff,0xd8,0xff,0xf7};expect(parse_jpegls_envelope(eight,8).prefix_size==8,"64-bit prefix");
    rejects([&]{parse_jpegls_envelope(four,6);},"decoded size exceeds allocation");
    rejects([&]{read_be32(four,SIZE_MAX);},"offset overflow");
    rejects([&]{parse_jpegls_envelope(Bytes{1,2,3},8);},"truncated envelope");
    CaptureState state;state.connected(4);rejects([&]{state.armed();},"arming before identity");serial[44]='0';state.identified(serial);state.armed();rejects([&]{state.receiving(5);},"generation invalidates capture");expect(state.state()==SessionState::faulted,"reset fault state");
    CaptureState s;s.connected(1);s.identified(serial);s.armed();s.receiving(1);rejects([&]{s.committed();},"no acknowledgement before commit");s.committing();s.committed();expect(s.state()==SessionState::identified,"durable commit rearm boundary");
    Command cmd;cmd.cdb=make_cdb(Direction::from_device,7,2);cmd.incoming_size=2;cmd.generation=1;
    ReplayTransport replay({ReplayStep{cmd,Reply{{0x12,0x34},1,0,0}}});replay.open(1);
    auto wrong=cmd;wrong.cdb[1]=8;rejects([&]{replay.submit(wrong);},"replay command mismatch");
    expect(replay.submit(cmd).data==Bytes({0x12,0x34}) && replay.finished(),"replay exact bytes");replay.reset(2);rejects([&]{replay.submit(cmd);},"stale command after reset");
    char temp[]="/tmp/valeo-tests-XXXXXX";const auto dir=::mkdtemp(temp);expect(dir!=nullptr,"private test directory");
    if(dir){const std::filesystem::path base(dir);auto file=base/"capture";{Journal j(file,4);j.append({1,2});rejects([&]{j.commit();},"incomplete capture");rejects([&]{j.append({3,4,5});},"overrun");j.append({3,4});j.commit();}
        expect(std::filesystem::file_size(file)==4 && !std::filesystem::exists(file.string()+".partial"),"atomic completed capture");
        rejects([&]{Journal j(file,4);},"existing capture preserved");
        {Journal j(base/"interrupted",4);j.append({9});}
        expect(std::filesystem::exists(base/"interrupted.partial") && !std::filesystem::exists(base/"interrupted"),"interrupted bytes preserved");
        auto race=base/"race";{Journal j(race,2);j.append({1,2});std::ofstream other(race);other<<"other";other.close();rejects([&]{j.commit();},"publish race cannot replace file");}
        expect(std::filesystem::file_size(race)==5,"racing capture unchanged");std::filesystem::remove_all(base);
    }
    std::cout<<(failures?"FAILED ":"PASS ")<<"protocol, bounds, identity, replay, reset, durable publication\n";
    return failures?1:0;
}
