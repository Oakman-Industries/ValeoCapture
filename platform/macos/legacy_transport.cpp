// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/legacy_transport.hpp"
#include "valeo/sbp2.hpp"
#include "valeo/identity.hpp"
#include "valeo/capture_config.hpp"
#include "valeo/protocol.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/firewire/IOFireWireSBP2Lib.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <limits>
#include <algorithm>

namespace valeo {
namespace {
template<class T> class CF {
    T p_{};
public:
    explicit CF(T p) : p_(p) {}
    ~CF() { if(p_)CFRelease(p_); }
    T get() const { return p_; }
    CF(const CF&)=delete;
    CF& operator=(const CF&)=delete;
};
class IOObject {
    io_object_t p_{};
public:
    explicit IOObject(io_object_t p=0) : p_(p) {}
    ~IOObject(){if(p_)IOObjectRelease(p_);}
    io_object_t get() const { return p_; }
    IOObject(const IOObject&)=delete;
    IOObject& operator=(const IOObject&)=delete;
};
std::uint64_t number(io_registry_entry_t entry, CFStringRef key) {
    CF<CFTypeRef> property(IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0));
    std::uint64_t n=0;
    if(property.get() && CFGetTypeID(property.get())==CFNumberGetTypeID())
        CFNumberGetValue(static_cast<CFNumberRef>(property.get()),kCFNumberSInt64Type,&n);
    return n;
}
void check(IOReturn code, const char* what) {
    if(code==kIOReturnSuccess)return;
    std::ostringstream message;message<<what<<" (IOKit 0x"<<std::hex<<static_cast<std::uint32_t>(code)<<")";
    throw Error(code==kIOReturnExclusiveAccess || code==kIOReturnBusy ? ErrorCode::busy : ErrorCode::io,message.str());
}
} // namespace
struct LegacyTransport::Impl {
    IOCFPlugInInterface** plugin{}; // retain until Stop closes the kernel connection
    IOFireWireSBP2LibLUNInterface** lun{};
    IOFireWireSBP2LibLoginInterface** login{};
    IOFireWireSBP2LibORBInterface** orb{};
    Bytes dma;
    std::unique_ptr<Sbp2Completion> completion;
    std::uint64_t token{};
    std::thread::id owner{std::this_thread::get_id()};
    std::uint32_t generation{};
    std::uint32_t frame_remaining{};
    bool frame_ready{};
    bool opened{}, dispatcher{}, login_finished{}, logged_in{}, dead{}, pending_parameter{}, logout_finished{};
    IOReturn login_status{kIOReturnNotReady};
    CFRunLoopRef loop{};
    void same_thread() {if(std::this_thread::get_id()!=owner)throw Error(ErrorCode::invalid,"SBP2 session used from another run-loop thread");}
    static void login_done(void* context,FWSBP2LoginCompleteParams* p) {
        auto& s=*static_cast<Impl*>(context);
        if(!p){s.login_status=kIOReturnError;s.login_finished=true;return;}
        s.generation=p->generation;s.login_status=p->status;
        if(p->status==kIOReturnSuccess && (!p->statusBlock || p->statusBlockLength<8 || p->statusBlockLength>32 || p->statusBlock->sbpStatus || (p->statusBlock->details & 0x38)))s.login_status=kIOReturnError;
        s.logged_in=s.login_status==kIOReturnSuccess;s.login_finished=true;
    }
    static void message(void* context,UInt32 type,void*) {
        auto& s=*static_cast<Impl*>(context);
        if(type==kIOMessageServiceIsTerminated || type==kIOMessageServiceIsSuspended || type==kIOMessageFWSBP2ReconnectFailed){s.dead=true;s.logged_in=false;}
        if(type==kIOMessageFWSBP2ReconnectComplete){s.dead=true;s.logged_in=false;} // explicit re-identification required
    }
    static void logout_done(void* context,FWSBP2LogoutCompleteParams*) {
        auto& s=*static_cast<Impl*>(context);s.logout_finished=true;s.logged_in=false;
    }
    static void command_done(void* context,FWSBP2NotifyParams* p) {
        auto& s=*static_cast<Impl*>(context);
        if(!s.completion || !p)return;
        s.completion->notify(reinterpret_cast<std::uintptr_t>(p->refCon),p->generation,
                             p->notificationEvent,static_cast<const std::uint8_t*>(p->message),p->length);
    }
    bool pump_until(bool& completed,std::chrono::milliseconds timeout) {
        const auto end=std::chrono::steady_clock::now()+timeout;
        while(!completed && !dead && std::chrono::steady_clock::now()<end)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true);
        return completed && !dead;
    }
};
LegacyTransport::LegacyTransport():impl_(new Impl){}
LegacyTransport::~LegacyTransport(){close();}
std::vector<Device> LegacyTransport::enumerate() {
    impl_->same_thread();
    if(__builtin_available(macOS 26.0,*))
        throw Error(ErrorCode::unsupported,"macOS 26+ needs the separate ASFireWire backend; this build targets macOS 10.15.7–15");
    io_iterator_t raw=0;
    check(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("IOFireWireSBP2LUN"),&raw),"enumerate SBP2 LUNs");
    IOObject iterator(raw);std::vector<Device> devices;
    while(auto object=IOIteratorNext(raw)) {
        IOObject entry(object);Device d;
        check(IORegistryEntryGetRegistryEntryID(object,&d.registry_id),"read registry identity");
        d.guid=number(object,CFSTR("GUID"));d.vendor=static_cast<std::uint32_t>(number(object,CFSTR("Vendor_ID")));
        d.command_set_spec=static_cast<std::uint32_t>(number(object,CFSTR("Command_Set_Spec_ID")));
        d.command_set=static_cast<std::uint32_t>(number(object,CFSTR("Command_Set")));
        d.device_type=static_cast<std::uint32_t>(number(object,CFSTR("Device_Type")));
        io_string_t path{};if(IORegistryEntryGetPath(object,kIOServicePlane,path)==kIOReturnSuccess)d.path=path;
        if(d.leaf_candidate())devices.push_back(std::move(d));
    }
    return devices;
}
void LegacyTransport::open(std::uint64_t registry_id) {
    auto& s=*impl_;s.same_thread();
    if(s.lun)throw Error(ErrorCode::busy,"Session is already open");
    // Re-enumerate before opening; a saved registry ID cannot bypass candidate checks.
    bool candidate=false;for(const auto& d:enumerate())candidate=candidate || d.registry_id==registry_id;
    if(!candidate)throw Error(ErrorCode::unsupported,"Registry object is not the supported Leaf SBP2 tuple");
    IOObject service(IOServiceGetMatchingService(kIOMasterPortDefault,IORegistryEntryIDMatching(registry_id)));
    if(!service.get())throw Error(ErrorCode::disconnected,"Device disappeared before open");
    SInt32 score=0;
    check(IOCreatePlugInInterfaceForService(service.get(),kIOFireWireSBP2LibTypeID,kIOCFPlugInInterfaceID,&s.plugin,&score),"create Apple's SBP2 plugin");
    try {
        const auto query=(*s.plugin)->QueryInterface(s.plugin,CFUUIDGetUUIDBytes(kIOFireWireSBP2LibLUNInterfaceID),reinterpret_cast<void**>(&s.lun));
        if(query || !s.lun)throw Error(ErrorCode::unsupported,"Apple SBP2 LUN interface unavailable");
        check((*s.lun)->open(s.lun),"open LUN exclusively (quit other camera software first)");s.opened=true;
        s.loop=CFRunLoopGetCurrent();CFRetain(s.loop);
        check((*s.lun)->addCallbackDispatcherToRunLoop(s.lun,s.loop),"attach SBP2 callback dispatcher");s.dispatcher=true;
        (*s.lun)->setMessageCallback(s.lun,&s,&Impl::message);
        s.login=reinterpret_cast<IOFireWireSBP2LibLoginInterface**>((*s.lun)->createLogin(s.lun,CFUUIDGetUUIDBytes(kIOFireWireSBP2LibLoginInterfaceID)));
        if(!s.login)throw Error(ErrorCode::io,"Create SBP2 login failed");
        s.dead=false;s.login_finished=false;s.logged_in=false;
        // Apple's 4.2.5 login starts with a FOUR-BYTE payload cap. Observed working
        // traffic uses 4096. Normal reads retain link/ARDMA
        // clipping; experimental raw reads use the reviewed quirk below.
        (*s.login)->setMaxPayloadSize(s.login,4096);
        (*s.login)->setLoginFlags(s.login,kFWSBP2ExclusiveLogin);
        (*s.login)->setLoginCallback(s.login,&s,&Impl::login_done);
        (*s.login)->setStatusNotify(s.login,&s,&Impl::command_done);
        check((*s.login)->submitLogin(s.login),"submit SBP2 login");
        if(!s.pump_until(s.login_finished,std::chrono::milliseconds(5000)))throw Error(ErrorCode::timeout,"SBP2 login timed out or device reset");
        check(s.login_status,"SBP2 login completion");
    } catch(...) {close();throw;}
}
Reply LegacyTransport::submit(const Command& cmd) {
    auto& s=*impl_;s.same_thread();cmd.validate();
    if(!s.logged_in || s.dead)throw Error(ErrorCode::disconnected,"No usable SBP2 session");
    if(cmd.generation!=s.generation)throw Error(ErrorCode::stale_generation,"SBP2 generation changed");
#ifndef VALEO_EXPERIMENTAL_READS
    throw Error(ErrorCode::unsupported,"This build has camera ORBs disabled; no command sent");
#else
    // Hardware-observed named READ selectors are nonpersistent requests. Permit
    // six exact zero-padded selectors and their immediately following 64-byte
    // response, plus the two fixed diagnostics. No general outgoing command.
    const bool selector=is_parameter_selector(cmd);
    const bool named_response=cmd.direction==Direction::from_device && cmd.cdb[1]==0 && cmd.incoming_size==64 && s.pending_parameter;
    const bool diagnostic=cmd.direction==Direction::from_device && ((cmd.cdb[1]==7 && cmd.incoming_size==88) || (cmd.cdb[1]==0x22 && cmd.incoming_size==480));
    bool arm=false,frame=false,setup=false;
#ifdef VALEO_EXPERIMENTAL_CAPTURE
    arm=is_reference_physical_arm(cmd) && !s.frame_remaining;
    setup=is_reference_physical_setup(cmd) && !s.frame_ready;
    frame=cmd.direction==Direction::from_device && cmd.cdb[1]==0x14 && s.frame_ready &&
          cmd.incoming_size==std::min<std::uint32_t>(0x80000,s.frame_remaining) && cmd.incoming_size>=2;
#endif
    if((s.pending_parameter && !named_response) || (!selector && !named_response && !diagnostic && !arm && !frame && !setup))
        throw Error(ErrorCode::unsupported,"Only fixed diagnostics and allowlisted named identity reads are enabled");
    if(s.orb || s.token==std::numeric_limits<std::uintptr_t>::max())
        throw Error(ErrorCode::busy,"ORB already active or session token space exhausted");
    const bool inbound=cmd.direction==Direction::from_device;
    const auto size=inbound?cmd.incoming_size:static_cast<std::uint32_t>(cmd.outgoing.size());
    s.dma=inbound?Bytes(size,0):cmd.outgoing;
    s.completion.reset(new Sbp2Completion(++s.token,s.generation));
    try {
        s.orb=reinterpret_cast<IOFireWireSBP2LibORBInterface**>((*s.login)->createORB(s.login,CFUUIDGetUUIDBytes(kIOFireWireSBP2LibORBInterfaceID)));
        if(!s.orb)throw Error(ErrorCode::io,"Cannot allocate command ORB");
        (*s.orb)->setRefCon(s.orb,reinterpret_cast<void*>(static_cast<std::uintptr_t>(s.token)));
        UInt32 flags=kFWSBP2CommandCompleteNotify|kFWSBP2CommandTransferDataFromTarget|
                     kFWSBP2CommandImmediate|kFWSBP2CommandNormalORB|kFWSBP2CommandCheckGeneration;
#ifdef VALEO_EXPERIMENTAL_CAPTURE
        // Reviewed Leaf raw-read quirk, not a generic write policy. Apple's
        // pinned calculateTransferSizeLog bypasses ARDMA/maxRec clipping for
        // this bit, but still clips to login max payload and FireWire speed.
        // Normal clipping with a 4096 cap timed out on this back; 4-byte cap
        // completed correctly but took 551 seconds. Reference raw flags 0x1027
        // use this bit; keep our additional generation guard (0x1227).
        if(frame)flags|=(1u<<12);
#endif
        (*s.orb)->setCommandFlags(s.orb,flags);
        (*s.orb)->setCommandGeneration(s.orb,cmd.generation);
        (*s.orb)->setCommandTimeout(s.orb,static_cast<UInt32>(cmd.timeout.count()));
        auto cdb=cmd.cdb;
        check((*s.orb)->setCommandBlock(s.orb,cdb.data(),static_cast<UInt32>(cdb.size())),"copy vendor CDB into ORB");
        FWSBP2VirtualRange range{s.dma.data(),size};
        // The reference uses TransferDataFromTarget in the ORB flags for its
        // selectors too, while mapping their payload kIODirectionOut. Preserve
        // that measured Leaf quirk only for these tightly allowlisted requests.
        check((*s.orb)->setCommandBuffersAsRanges(s.orb,&range,1,inbound?kIODirectionIn:kIODirectionOut,0,size),"map owned diagnostic DMA buffer");
        check((*s.login)->submitORB(s.login,s.orb),"submit immediate read ORB");
        const auto deadline=std::chrono::steady_clock::now()+cmd.timeout+std::chrono::milliseconds(1000);
        while(!s.dead && s.completion->state()==CompletionState::pending && std::chrono::steady_clock::now()<deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode,0.01,true);
        if(s.dead || s.completion->state()==CompletionState::reset)
            throw Error(ErrorCode::stale_generation,"Device disconnected or bus generation changed during ORB");
        if(s.completion->state()==CompletionState::pending || s.completion->state()==CompletionState::timeout)
            throw Error(ErrorCode::timeout,"Read ORB timed out; session will be torn down before buffer release");
        if(s.completion->state()!=CompletionState::success)
            throw Error(ErrorCode::protocol,"Target returned malformed, failed or unrecognized SBP2 status");
        check((*s.orb)->releaseCommandBuffers(s.orb),"revoke completed DMA mapping");
        (*s.orb)->Release(s.orb);s.orb=nullptr;s.completion.reset();
        // Apple's interface has no residual-byte-count field. The protocol must
        // validate the returned structure; vector length is allocation, not proof
        // that the target wrote every byte. Preserve raw input for validation.
        s.pending_parameter=selector;
        if(arm){s.frame_remaining=reference_frame_bytes;s.frame_ready=false;}
        if(diagnostic && cmd.cdb[1]==7 && s.frame_remaining==reference_frame_bytes)
            s.frame_ready=read_be32(s.dma,0)==reference_frame_bytes;
        if(frame){s.frame_remaining-=size;if(!s.frame_remaining)s.frame_ready=false;}
        return Reply{inbound?std::move(s.dma):Bytes{},s.generation,0,0};
    } catch(...) {close();throw;}
#endif
}
void LegacyTransport::close() noexcept {
    auto& s=*impl_;
    // A successful command can still be the kernel login's retained fLastORB.
    // 4.2.5 releaseLogin iterates the ORB set; releasing the login before logout
    // clears fLastORB can spin there until physical disconnect. Complete the
    // management logout while its callback dispatcher is STILL attached.
    if(s.login && s.logged_in && !s.dead && s.dispatcher) {
        s.logout_finished=false;
        (*s.login)->setLogoutCallback(s.login,&s,&Impl::logout_done);
        if((*s.login)->submitLogout(s.login)==kIOReturnSuccess)
            s.pump_until(s.logout_finished,std::chrono::milliseconds(5000));
        (*s.login)->setLogoutCallback(s.login,nullptr,nullptr);
    }
    if(s.lun && s.dispatcher){
        (*s.lun)->setMessageCallback(s.lun,nullptr,nullptr);
        (*s.lun)->removeCallbackDispatcherFromRunLoop(s.lun);s.dispatcher=false;
    }
    if(s.login){(*s.login)->setStatusNotify(s.login,nullptr,nullptr);(*s.login)->setLoginCallback(s.login,nullptr,nullptr);}
    if(s.orb){
        // Retain dma even if unmapping fails, through destruction of the user
        // client below. Its Stop closes the kernel connection and flushes ORBs.
        (*s.orb)->releaseCommandBuffers(s.orb);
        (*s.orb)->Release(s.orb);s.orb=nullptr;
    }
    if(s.login){
        // Graceful logout above clears the retained last ORB first. On physical
        // disconnect the kernel's reconnect/termination path clears it instead.
        (*s.login)->Release(s.login);s.login=nullptr;
    }
    if(s.lun){
        if(s.opened)(*s.lun)->close(s.lun);
        (*s.lun)->Release(s.lun);s.lun=nullptr;
    }
    if(s.plugin){IODestroyPlugInInterface(s.plugin);s.plugin=nullptr;}
    s.completion.reset();s.dma.clear();
    if(s.loop){CFRelease(s.loop);s.loop=nullptr;}
    s.dispatcher=false;s.opened=false;s.logged_in=false;s.dead=false;s.pending_parameter=false;
    s.frame_remaining=0;s.frame_ready=false;
}
std::uint32_t LegacyTransport::generation() const noexcept{return impl_->generation;}
} // namespace valeo
