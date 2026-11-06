// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/identity.hpp"
#include "valeo/protocol.hpp"
#include <algorithm>

namespace valeo {
std::string parameter_name(Parameter value) {
    switch(value) {
        case Parameter::camera_type:return "camera_type";
        case Parameter::serial_revision:return "serial_revision";
        case Parameter::body_serial:return "camera_body_serial";
        case Parameter::sensor_size:return "sensor_size";
        case Parameter::sensor_color:return "sensor_color";
        case Parameter::firmware_version:return "firmware_version";
    }
    throw Error(ErrorCode::invalid,"Unsupported named identity parameter");
}
Command parameter_selector(Parameter value,std::uint32_t generation) {
    Command c;c.direction=Direction::to_device;c.outgoing.assign(36,0);
    const auto name=parameter_name(value);
    std::copy(name.begin(),name.end(),c.outgoing.begin()+4);
    c.cdb=make_cdb(c.direction,0x11,36);c.generation=generation;return c;
}
bool is_parameter_selector(const Command& c) {
    if(c.direction!=Direction::to_device || c.cdb!=make_cdb(Direction::to_device,0x11,36) ||
       c.outgoing.size()!=36 || c.incoming_size)return false;
    for(auto p:{Parameter::camera_type,Parameter::serial_revision,Parameter::body_serial,Parameter::sensor_size,Parameter::sensor_color,Parameter::firmware_version})
        if(c.outgoing==parameter_selector(p,c.generation).outgoing)return true;
    return false;
}
namespace {
void successful(const Reply& r,std::uint32_t generation,std::size_t size) {
    if(r.generation!=generation)throw Error(ErrorCode::stale_generation,"Generation changed during identity query");
    if(r.sbp_status || r.response || r.data.size()!=size)throw Error(ErrorCode::protocol,"Identity query failed or returned the wrong allocation size");
}
std::string text_field(const Bytes& bytes,std::size_t start) {
    const auto begin=bytes.begin()+static_cast<std::ptrdiff_t>(start),end=begin+32;
    const auto nul=std::find(begin,end,0);
    if(nul==end || nul==begin || std::any_of(begin,nul,[](std::uint8_t c){return c<32 || c>126;}))
        throw Error(ErrorCode::protocol,"Malformed bounded identity text field");
    return std::string(begin,nul);
}
}
Bytes IdentityReader::query(Parameter parameter) {
    if(transport_.generation()!=generation_)throw Error(ErrorCode::stale_generation,"Identity session generation changed");
    const auto select=parameter_selector(parameter,generation_);
    successful(transport_.submit(select),generation_,0);
    if(transport_.generation()!=generation_)throw Error(ErrorCode::stale_generation,"Reset between identity selector and response");
    Command read;read.cdb=make_cdb(Direction::from_device,0,64);read.incoming_size=64;read.generation=generation_;
    const auto result=transport_.submit(read);successful(result,generation_,64);
    if(transport_.generation()!=generation_)throw Error(ErrorCode::stale_generation,"Reset after identity response");
    auto expected=parameter_name(parameter);for(auto& c:expected)if(c>='a' && c<='z')c=static_cast<char>(c-'a'+'A');
    if(text_field(result.data,0)!=expected)throw Error(ErrorCode::protocol,"Identity response belongs to a different selector");
    return result.data;
}
std::string IdentityReader::read(Parameter parameter) {
    if(parameter==Parameter::firmware_version)throw Error(ErrorCode::invalid,"Firmware version is binary; use the typed query");
    return text_field(query(parameter),32);
}
std::uint32_t IdentityReader::firmware_version() {
    return read_be32(query(Parameter::firmware_version),32);
}
bool Identity::is_valeo11() const noexcept {
    return camera_type=="VA11" && (revision=="A" || revision=="M") && !body_serial.empty() &&
           sensor_size.size()==1 && sensor_size[0]>='0' && sensor_size[0]<='9' && (sensor_color=="C" || sensor_color=="M");
}
Identity IdentityReader::identify() {
    Identity result;result.camera_type=read(Parameter::camera_type);result.revision=read(Parameter::serial_revision);
    if(result.camera_type!="VA11" || (result.revision!="A" && result.revision!="M"))
        throw Error(ErrorCode::unsupported,"Named parameters identify a different back family");
    result.body_serial=read(Parameter::body_serial);result.sensor_size=read(Parameter::sensor_size);result.sensor_color=read(Parameter::sensor_color);
    if(!result.is_valeo11())throw Error(ErrorCode::unsupported,"Identity fields do not identify a supported Valeo 11");
    return result;
}
} // namespace valeo
