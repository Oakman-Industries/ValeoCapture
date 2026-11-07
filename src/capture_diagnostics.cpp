// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/capture_diagnostics.hpp"
#include "valeo/protocol.hpp"

namespace valeo {
namespace {
std::string hex(const Bytes& data) {
    static constexpr char digits[]="0123456789abcdef";
    std::string out;out.reserve(data.size()*2);
    for(const auto byte:data){out+=digits[byte>>4];out+=digits[byte&15];}
    return out;
}
std::string quote(const std::string& s) {
    std::string out="\"";
    for(const char ch:s) {
        const auto c=static_cast<unsigned char>(ch);
        if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}
        else if(c<32){static constexpr char digits[]="0123456789abcdef";out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}
        else out+=static_cast<char>(c);
    }
    return out+'"';
}
} // namespace

bool capture_parameters_report_exposure(const Bytes& capture_parameters) {
    return read_be32(capture_parameters,kCaptureParametersExposureOffset)!=0;
}

std::uint64_t count_non_empty_samples(const Bytes& data) {
    std::uint64_t count=0;
    std::size_t i=0;
    for(;i+1<data.size();i+=2)if(data[i]!=0xFF || data[i+1]!=0xFF)++count;
    if(i<data.size() && data[i]!=0xFF)++count;
    return count;
}

std::string failed_exposure_json(const FailedExposureRecord& r) {
    return "{\"schema\":1,\"kind\":\"failed-exposure\",\"time\":"+quote(r.time)+
        ",\"message\":"+quote(kFailedExposureMessage)+
        ",\"evidence\":\"capture-parameter word 11 (offset 0x2C) is 0: the back recorded no exposure\""+
        ",\"frameBytes\":"+std::to_string(r.frame_bytes)+",\"nonEmptySamples\":"+std::to_string(r.non_empty_samples)+
        ",\"busGeneration\":"+std::to_string(r.bus_generation)+",\"softwareISO\":"+std::to_string(r.software_iso)+
        ",\"readyStatus\":"+quote(hex(r.ready_status))+",\"captureParameters\":"+quote(hex(r.capture_parameters))+
        ",\"auxiliaryNonEmptySamples\":"+std::to_string(r.auxiliary_non_empty_samples)+
        ",\"auxiliaryParameters\":"+quote(hex(r.auxiliary_parameters))+"}\n";
}
} // namespace valeo
