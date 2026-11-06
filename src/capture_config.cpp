// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_config.hpp"
#include "valeo/protocol.hpp"
#include <algorithm>
namespace valeo {
Command physical_arm(std::uint32_t generation) {
    Command c;c.direction=Direction::to_device;c.outgoing.assign(40,0);c.generation=generation;
    const std::uint32_t words[]={1,1,0,0,0,0,0,reference_width,reference_height,reference_frame_bytes};
    for(std::size_t i=0;i<10;++i)for(unsigned j=0;j<4;++j)c.outgoing[i*4+j]=static_cast<std::uint8_t>(words[i]>>(24-j*8));
    c.cdb=make_cdb(c.direction,0x12,40);return c;
}
bool is_reference_physical_arm(const Command& c) {
    const auto expected=physical_arm(c.generation);
    return c.direction==expected.direction && c.cdb==expected.cdb && c.outgoing==expected.outgoing && !c.incoming_size;
}
Command auxiliary_arm(std::uint32_t generation) {
    auto c=physical_arm(generation);c.outgoing[7]=2;return c;
}
Command auxiliary_trigger(std::uint32_t generation) {
    Command c;c.direction=Direction::to_device;c.generation=generation;
    c.outgoing.assign(56,0);c.cdb=make_cdb(c.direction,0x13,56);return c;
}
Command timed_light_rearm(std::uint32_t generation,std::uint32_t status_time) {
    auto c=physical_arm(generation);const auto time=status_time/1000;
    for(unsigned i=0;i<4;++i)c.outgoing[8+i]=static_cast<std::uint8_t>(time>>(24-i*8));
    return c;
}
bool is_reference_auxiliary_arm(const Command& c) {
    const auto expected=auxiliary_arm(c.generation);
    return c.direction==expected.direction && c.cdb==expected.cdb && c.outgoing==expected.outgoing && !c.incoming_size;
}
bool is_reference_auxiliary_trigger(const Command& c) {
    const auto expected=auxiliary_trigger(c.generation);
    return c.direction==expected.direction && c.cdb==expected.cdb && c.outgoing==expected.outgoing && !c.incoming_size;
}
Command select_verified_iso(std::uint16_t iso,std::uint32_t generation) {
    if(std::find(valeo11_iso_values.begin(),valeo11_iso_values.end(),iso)==valeo11_iso_values.end())
        throw Error(ErrorCode::unsupported,"Valeo11 supports ISO25,50,100,200");
    Command c;c.direction=Direction::to_device;c.generation=generation;c.outgoing={0,static_cast<std::uint8_t>(iso)};
    c.cdb=make_cdb(c.direction,0x20,2);return c;
}
bool is_reference_iso(const Command& c) {
    for(auto iso:valeo11_iso_values){const auto expected=select_verified_iso(iso,c.generation);
        if(c.direction==expected.direction && c.cdb==expected.cdb && c.outgoing==expected.outgoing && !c.incoming_size)return true;}
    return false;
}
Command physical_setup(PhysicalSetup setting,std::uint32_t generation) {
    Command c;c.direction=Direction::to_device;c.generation=generation;
    switch(setting) {
        case PhysicalSetup::release_interface:c.outgoing={0,2};c.cdb=make_cdb(c.direction,0x1c,2);break;
        case PhysicalSetup::rapid_mode:c.outgoing={0,0,0,2,0,0,0,10};c.cdb=make_cdb(c.direction,0x21,8);break;
        case PhysicalSetup::ready_beep:c.outgoing={0,0,3,32,0,0,0,150};c.cdb=make_cdb(c.direction,9,8);break;
        default:throw Error(ErrorCode::invalid,"Unknown physical setup request");
    }
    return c;
}
bool is_reference_physical_setup(const Command& c) {
    for(auto step:{PhysicalSetup::release_interface,PhysicalSetup::rapid_mode,PhysicalSetup::ready_beep}) {
        const auto expected=physical_setup(step,c.generation);
        if(c.direction==expected.direction && c.cdb==expected.cdb && c.outgoing==expected.outgoing && !c.incoming_size)return true;
    }
    return false;
}
} // namespace valeo
