// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/identity.hpp"
#include "valeo/protocol.hpp"
#include "valeo/replay.hpp"
#include "valeo/capture_config.hpp"
#include <iostream>

using namespace valeo;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
ReplayStep response(Parameter p,const std::string& value) {
    auto key=parameter_name(p);for(auto& c:key)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
    Bytes data(64,0);std::copy(key.begin(),key.end(),data.begin());std::copy(value.begin(),value.end(),data.begin()+32);
    Command c;c.cdb=make_cdb(Direction::from_device,0,64);c.incoming_size=64;c.generation=1;
    return {c,Reply{data,1,0,0}};
}
void append(std::deque<ReplayStep>& steps,Parameter p,const std::string& value) {
    steps.push_back({parameter_selector(p,1),Reply{{},1,0,0}});steps.push_back(response(p,value));
}
int main(){try{
    std::deque<ReplayStep> steps;append(steps,Parameter::camera_type,"VA11");append(steps,Parameter::serial_revision,"A");
    append(steps,Parameter::body_serial,"00000000000");append(steps,Parameter::sensor_size,"5");append(steps,Parameter::sensor_color,"C");
    ReplayTransport t(std::move(steps));t.open(1);auto id=IdentityReader(t).identify();require(id.is_valeo11()&&t.finished(),"Identity sequence or model proof failed");
    auto selector=parameter_selector(Parameter::camera_type,1);require(is_parameter_selector(selector),"Valid selector refused");
    selector.outgoing.back()=1;require(!is_parameter_selector(selector),"Uninitialized selector padding accepted");
    selector=parameter_selector(Parameter::camera_type,1);selector.outgoing[4]='x';require(!is_parameter_selector(selector),"Unknown selector accepted");
    auto arm=physical_arm(1);require(is_reference_physical_arm(arm) && read_be32(arm.outgoing,28)==4056 && read_be32(arm.outgoing,32)==2684 && read_be32(arm.outgoing,36)==21772608,"Observed full-frame physical configuration mismatch");
    arm.outgoing[8]=1;require(!is_reference_physical_arm(arm),"Unreviewed physical configuration accepted");
    for(auto step:{PhysicalSetup::release_interface,PhysicalSetup::rapid_mode,PhysicalSetup::ready_beep}) {
        auto setting=physical_setup(step,1);require(is_reference_physical_setup(setting),"Observed startup setting refused");
        setting.outgoing.back()^=1;require(!is_reference_physical_setup(setting),"Unreviewed startup value accepted");
    }
    std::deque<ReplayStep> other;append(other,Parameter::camera_type,"OTHER");append(other,Parameter::serial_revision,"A");ReplayTransport wrong(std::move(other));wrong.open(1);
    bool refused=false;try{IdentityReader(wrong).identify();}catch(const Error& e){refused=e.code==ErrorCode::unsupported;}require(refused && wrong.finished(),"Wrong back was not stopped before further reads");
    auto malformed=response(Parameter::camera_type,"VA11");malformed.reply.data[0]='x';ReplayTransport mismatched({{parameter_selector(Parameter::camera_type,1),Reply{{},1,0,0}},malformed});mismatched.open(1);
    refused=false;try{IdentityReader(mismatched).read(Parameter::camera_type);}catch(const Error&){refused=true;}require(refused,"Mismatched response echo accepted");
    malformed=response(Parameter::camera_type,"VA11");std::fill(malformed.reply.data.begin()+32,malformed.reply.data.end(),'x');
    ReplayTransport unterminated({{parameter_selector(Parameter::camera_type,1),Reply{{},1,0,0}},malformed});unterminated.open(1);
    refused=false;try{IdentityReader(unterminated).read(Parameter::camera_type);}catch(const Error&){refused=true;}require(refused,"Unterminated identity value accepted");
    auto firmware=response(Parameter::firmware_version,"unused");std::fill(firmware.reply.data.begin()+32,firmware.reply.data.end(),0);
    firmware.reply.data[34]=0x0c;firmware.reply.data[35]=0xe5; // BE32 3301, not an empty string
    ReplayTransport fw({{parameter_selector(Parameter::firmware_version,1),Reply{{},1,0,0}},firmware});fw.open(1);
    require(IdentityReader(fw).firmware_version()==3301 && fw.finished(),"Binary firmware version was decoded as text or host endian");
    std::cout<<"PASS identity: allowlisted selector/read pairs, zero padding, exact echo, bounded text, wrong-family refusal\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
