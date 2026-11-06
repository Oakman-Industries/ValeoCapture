// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/command_gate.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/identity.hpp"
#include "valeo/protocol.hpp"
#include <iostream>

using namespace valeo;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static Command read(std::uint8_t operation,std::uint32_t size){Command c;c.cdb=make_cdb(Direction::from_device,operation,size);c.incoming_size=size;c.generation=1;return c;}
int main(){try{
    ReferenceCommandGate diagnostic;
    require(!diagnostic.allows(physical_arm(1)),"Diagnostic policy armed camera");
    auto response=read(0,64);require(!diagnostic.allows(response),"Unsolicited named response allowed");
    auto selector=parameter_selector(Parameter::camera_type,1);require(diagnostic.allows(selector),"Exact selector refused");
    diagnostic.completed(selector,{});require(diagnostic.allows(response),"Paired response refused");
    require(!diagnostic.allows(selector) && !diagnostic.allows(read(7,88)),"Named transaction was interrupted");
    diagnostic.completed(response,Bytes(64));require(diagnostic.allows(read(7,88)),"Diagnostic status refused");
    ReferenceCommandGate capture(true);
    require(capture.allows(select_verified_iso(25,1)) && capture.allows(select_verified_iso(50,1)) && !diagnostic.allows(select_verified_iso(25,1)),"ISO gate enabled diagnostics or refused traced values");
    for(auto iso:valeo11_iso_values) {
        const auto setting=select_verified_iso(iso,1);
        require(setting.outgoing==Bytes({0,static_cast<std::uint8_t>(iso)}) && capture.allows(setting) && !diagnostic.allows(setting),"ISO table/framing or diagnostic isolation changed");
    }
    bool rejected=false;try{select_verified_iso(400,1);}catch(const Error&){rejected=true;}
    require(rejected,"Unlisted ISO allowed");
    auto unknown_iso=select_verified_iso(25,1);unknown_iso.outgoing={1,25};
    require(!capture.allows(unknown_iso),"High-byte ISO payload escaped allowlist");
    auto chunk=read(0x14,0x80000);require(!capture.allows(chunk),"Frame read before arm allowed");
    auto arm=physical_arm(1);capture.completed(arm,{});require(!capture.allows(chunk),"Frame read before readiness allowed");
    require(!capture.allows(select_verified_iso(50,1)),"ISO changed during an armed frame");
    require(!capture.allows(selector) && !capture.allows(arm),"Identity/arm interrupted active frame");
    require(capture.allows(physical_setup(PhysicalSetup::ready_beep,1)),"Reviewed startup after arm refused");
    auto status=read(7,88);capture.completed(status,Bytes(88));require(!capture.allows(chunk),"Idle treated as ready");
    require(capture.allows(select_verified_iso(200,1)),"Fresh idle status did not allow live ISO change");
    capture.completed(select_verified_iso(200,1),{});
    require(!capture.allows(select_verified_iso(100,1)),"A live ISO change reused a stale idle check");
    capture.completed(status,Bytes(88));require(capture.allows(select_verified_iso(100,1)),"New idle poll did not permit next change");
    Bytes ready(88);ready[0]=1;ready[1]=0x4c;ready[2]=0x39;ready[3]=0x40;capture.completed(status,ready);
    require(capture.allows(chunk) && !capture.allows(read(0x14,512)),"Chunk policy did not use remaining size");
    require(!capture.allows(select_verified_iso(25,1)),"Ready image was interrupted by ISO");
    capture.completed(status,Bytes(88));capture.completed(status,Bytes(88));
    require(!capture.allows(select_verified_iso(25,1)),"Zero status after a ready announcement reopened ISO changes");
    capture.completed(status,ready);
    require(!capture.allows(physical_setup(PhysicalSetup::ready_beep,1)),"Setting interrupted ready frame");
    for(unsigned i=0;i<41;++i)capture.completed(chunk,Bytes(0x80000));
    require(!capture.allows(chunk) && capture.allows(read(0x14,276800)),"Final chunk boundary incorrect");
    capture.completed(read(0x14,276800),Bytes(276800));require(!capture.allows(chunk) && capture.allows(arm),"Completed frame did not end/rearm");
    auto unknown=physical_setup(PhysicalSetup::release_interface,1);unknown.outgoing[1]=3;
    require(!capture.allows(unknown),"Unreviewed setting accepted");
    capture.completed(arm,{});capture.reset();require(!capture.allows(chunk) && capture.allows(selector),"Reset reused frame state");
    ReferenceCommandGate small(true,61440);small.completed(arm,{});small.completed(status,ready);
    require(!small.allows(chunk) && small.allows(read(0x14,61440)),"Backend DMA ceiling not enforced");
    small.completed(read(0x14,61440),Bytes(61440));
    auto aux=auxiliary_arm(1),trigger=auxiliary_trigger(1);
    ReferenceCommandGate recovering(true);
    recovering.completed(status,ready);require(recovering.allows(chunk) && !recovering.allows(arm),"Pending frame was not protected for drain");
    diagnostic.completed(status,ready);require(!diagnostic.allows(chunk),"Diagnostics adopted a pending frame");
    require(!capture.allows(aux) && !capture.allows(trigger),"Auxiliary enabled by capture alone");
    ReferenceCommandGate paired(true,0x80000,true);
    require(!paired.allows(aux) && !paired.allows(trigger),"Auxiliary before a light drain allowed");
    auto drain=[&](ReferenceCommandGate& gate){for(unsigned i=0;i<41;++i)gate.completed(chunk,Bytes(0x80000));gate.completed(read(0x14,276800),Bytes(276800));};
    paired.completed(arm,{});paired.completed(status,ready);drain(paired);
    require(paired.allows(aux) && !paired.allows(trigger),"Light drain did not permit only auxiliary arm");
    paired.completed(aux,{});paired.completed(status,ready);
    paired.completed(status,Bytes(88));require(!paired.allows(select_verified_iso(50,1)),"Auxiliary wait permitted an ISO change");
    require(!paired.allows(chunk) && paired.allows(trigger),"Auxiliary read before internal trigger allowed");
    require(!paired.allows(arm) && !paired.allows(physical_setup(PhysicalSetup::ready_beep,1)),"Auxiliary sequence interrupted by light/setup");
    auto pointer_scratch=trigger;pointer_scratch.outgoing[8]=1;
    require(!paired.allows(pointer_scratch),"Uninitialized/host-pointer scratch allowed");
    paired.completed(trigger,{});require(!paired.allows(trigger),"Repeated auxiliary trigger allowed");
    auto timed=ready;const std::uint32_t ticks=1243864;
    for(unsigned i=0;i<4;++i)timed[0x38+i]=static_cast<std::uint8_t>(ticks>>(24-i*8));
    paired.completed(status,timed);require(paired.allows(chunk),"Ready auxiliary read refused");drain(paired);
    auto final_arm=timed_light_rearm(1,ticks);
    require(read_be32(final_arm.outgoing,8)==1243,"Status timing division/encoding disagrees with reference");
    require(paired.allows(final_arm) && !paired.allows(timed_light_rearm(1,ticks+1000)) && !paired.allows(arm),"Final arm did not enforce observed auxiliary timing");
    paired.completed(final_arm,{});require(paired.allows(physical_setup(PhysicalSetup::ready_beep,1)),"Final ready tone refused");
    paired.reset();require(!paired.allows(aux) && !paired.allows(final_arm),"Reset retained auxiliary history");
    std::cout<<"PASS command policy: named transaction, disabled capture, arm/readiness, exact chunks, settings isolation and reset\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
