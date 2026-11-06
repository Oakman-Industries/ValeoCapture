// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/jpegls.hpp"
#include "valeo/protocol.hpp"
#include <charls/charls.h>
#include <memory>

namespace valeo {
namespace {
void checked(charls_jpegls_errc code){if(code!=charls::jpegls_errc::success)throw Error(ErrorCode::protocol,std::string("JPEG-LS: ")+charls_get_error_message(code));}
}
std::vector<std::uint16_t> decode_gain_jpegls(const Bytes& source,std::uint32_t width,std::uint32_t height){
    const auto count=std::uint64_t(width)*height;
    if(!width||!height||count>128ull*1024*1024)throw Error(ErrorCode::invalid,"Gain dimensions exceed validated bounds");
    const auto size=static_cast<std::uint32_t>(count*2);
    const auto envelope=parse_jpegls_envelope(source,size);
    if(envelope.decoded_bytes!=size)throw Error(ErrorCode::protocol,"Gain length does not match sensor dimensions");
    std::unique_ptr<charls_jpegls_decoder,decltype(&charls_jpegls_decoder_destroy)> decoder(charls_jpegls_decoder_create(),&charls_jpegls_decoder_destroy);
    if(!decoder)throw Error(ErrorCode::io,"JPEG-LS decoder allocation failed");
    checked(charls_jpegls_decoder_set_source_buffer(decoder.get(),source.data()+envelope.prefix_size,source.size()-envelope.prefix_size));
    checked(charls_jpegls_decoder_read_header(decoder.get()));charls_frame_info info{};
    checked(charls_jpegls_decoder_get_frame_info(decoder.get(),&info));
    if(info.width!=width||info.height!=height||info.component_count!=1||info.bits_per_sample<9||info.bits_per_sample>16)throw Error(ErrorCode::protocol,"Unexpected JPEG-LS gain geometry/depth/components");
    std::int32_t near_lossless=0;checked(charls_jpegls_decoder_get_near_lossless(decoder.get(),0,&near_lossless));
    if(near_lossless!=0)throw Error(ErrorCode::protocol,"Lossy JPEG-LS cannot be used for gain calibration");
    std::size_t required=0;checked(charls_jpegls_decoder_get_destination_size(decoder.get(),0,&required));
    if(required!=size)throw Error(ErrorCode::protocol,"JPEG-LS destination size mismatch");
    std::vector<std::uint16_t> output(static_cast<std::size_t>(count));
    checked(charls_jpegls_decoder_decode_to_buffer(decoder.get(),output.data(),required,0));return output;
}
} // namespace valeo
