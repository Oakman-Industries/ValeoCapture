// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/scsi_transport.hpp"
#include "valeo/command_gate.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/scsi/SCSITaskLib.h>
#include <IOKit/firewire/IOFireWireLib.h>
#include <thread>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace valeo {
namespace {
void check(IOReturn result,const char* action) {
    if(result==kIOReturnSuccess)return;
    std::ostringstream message;message<<action<<" (IOKit0x"<<std::hex<<static_cast<std::uint32_t>(result)<<")";
    throw Error(result==kIOReturnBusy || result==kIOReturnExclusiveAccess?ErrorCode::busy:ErrorCode::io,message.str());
}
std::uint64_t number(io_registry_entry_t entry,CFStringRef key) {
    auto value=IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0);std::uint64_t result=0;
    if(value && CFGetTypeID(value)==CFNumberGetTypeID())CFNumberGetValue(static_cast<CFNumberRef>(value),kCFNumberSInt64Type,&result);
    if(value)CFRelease(value);return result;
}
io_registry_entry_t ancestor(io_registry_entry_t entry,const char* kind) {
    IOObjectRetain(entry);
    for(unsigned depth=0;depth<32;++depth) {
        if(IOObjectConformsTo(entry,kind))return entry;
        io_registry_entry_t parent=0;const auto result=IORegistryEntryGetParentEntry(entry,kIOServicePlane,&parent);
        IOObjectRelease(entry);if(result!=KERN_SUCCESS)return 0;entry=parent;
    }
    IOObjectRelease(entry);return 0;
}
std::string text(io_registry_entry_t entry,CFStringRef key) {
    auto value=IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0);std::string result;
    if(value && CFGetTypeID(value)==CFStringGetTypeID()) {
        char buffer[256]{};
        if(CFStringGetCString(static_cast<CFStringRef>(value),buffer,sizeof(buffer),kCFStringEncodingUTF8))result=buffer;
    }
    if(value)CFRelease(value);return result;
}
bool has_number(io_registry_entry_t entry,CFStringRef key) {
    auto value=IORegistryEntryCreateCFProperty(entry,key,kCFAllocatorDefault,0);
    const bool ok=value && CFGetTypeID(value)==CFNumberGetTypeID();if(value)CFRelease(value);return ok;
}
// Which kernel stack owns the Leaf LUN. Apple: IOFireWireSBP2LUN →
// IOSCSIPeripheralDeviceNub (macOS 10.15.7–15). ASFW (macOS 26+, DriverKit
// net.asfw.driver): ASFWSCSIController → IOSCSITargetDevice →
// IOSCSIHierarchicalLogicalUnit, with the unit's Config ROM identity on a
// sibling ASFWSBP2Nub under the same FireWire controller PCI device.
enum class Stack {apple,asfw};
struct Candidate {Device device;io_service_t nub{};Stack stack{Stack::apple};};
constexpr const char* kAsfwServer="net.asfw.driver";
bool asfw_service(io_registry_entry_t entry){return text(entry,CFSTR("IOUserServerName"))==kAsfwServer;}
std::vector<Candidate> apple_candidates() {
    io_iterator_t iterator=0;check(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("IOSCSIPeripheralDeviceNub"),&iterator),"enumerate SCSI peripherals");
    std::vector<Candidate> result;
    while(auto entry=IOIteratorNext(iterator)) {
        auto lun=ancestor(entry,"IOFireWireSBP2LUN");
        if(lun) {
            Device d;IORegistryEntryGetRegistryEntryID(lun,&d.registry_id);
            d.vendor=static_cast<std::uint32_t>(number(lun,CFSTR("Vendor_ID")));
            d.command_set_spec=static_cast<std::uint32_t>(number(lun,CFSTR("Command_Set_Spec_ID")));
            d.command_set=static_cast<std::uint32_t>(number(lun,CFSTR("Command_Set")));
            d.device_type=static_cast<std::uint32_t>(number(lun,CFSTR("Device_Type")));d.guid=number(lun,CFSTR("GUID"));
            io_string_t path{};if(IORegistryEntryGetPath(lun,kIOServicePlane,path)==KERN_SUCCESS)d.path=path;
            IOObjectRelease(lun);
            if(d.leaf_candidate()){result.push_back({std::move(d),entry,Stack::apple});continue;}
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);return result;
}
// The single ASFW SBP-2 unit nub under a FireWire controller. ASFW bridges
// target 0 to one SBP-2 session, so more than one unit on the controller is
// ambiguous and refused rather than guessed.
io_registry_entry_t asfw_unit_nub(io_registry_entry_t pci) {
    io_iterator_t iterator=0;
    if(IORegistryEntryCreateIterator(pci,kIOServicePlane,kIORegistryIterateRecursively,&iterator)!=KERN_SUCCESS)return 0;
    io_registry_entry_t found=0;unsigned count=0;
    while(auto entry=IOIteratorNext(iterator)) {
        if(asfw_service(entry) && text(entry,CFSTR("ASFWNubType"))=="SBP2") {
            ++count;if(!found){found=entry;continue;}
        }
        IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);
    if(count!=1){if(found)IOObjectRelease(found);return 0;}
    return found;
}
std::vector<Candidate> asfw_candidates() {
    io_iterator_t iterator=0;check(IOServiceGetMatchingServices(kIOMasterPortDefault,IOServiceMatching("IOSCSIHierarchicalLogicalUnit"),&iterator),"enumerate ASFW SCSI logical units");
    std::vector<Candidate> result;
    while(auto entry=IOIteratorNext(iterator)) {
        bool kept=false;
        auto controller=ancestor(entry,"IOUserSCSIParallelInterfaceController");
        if(controller && asfw_service(controller) && text(controller,CFSTR("IOUserClass"))=="ASFWSCSIController" &&
           text(entry,CFSTR("SCSITaskDeviceCategory"))=="SCSITaskUserClientDevice" &&
           number(entry,CFSTR("Peripheral Device Type"))==2 && number(entry,CFSTR("SCSI Logical Unit Number"))==0) {
            auto pci=ancestor(controller,"IOPCIDevice");
            auto unit=pci?asfw_unit_nub(pci):0;
            if(unit && has_number(unit,CFSTR("ASFWBusGeneration")) && has_number(unit,CFSTR("Command_Set"))) {
                Device d;IORegistryEntryGetRegistryEntryID(unit,&d.registry_id);
                d.vendor=static_cast<std::uint32_t>(number(unit,CFSTR("Vendor_ID")));
                d.command_set_spec=static_cast<std::uint32_t>(number(unit,CFSTR("Command_Set_Spec_ID")));
                d.command_set=static_cast<std::uint32_t>(number(unit,CFSTR("Command_Set")));
                d.device_type=static_cast<std::uint32_t>(number(unit,CFSTR("Device_Type")));d.guid=number(unit,CFSTR("GUID"));
                io_string_t path{};if(IORegistryEntryGetPath(unit,kIOServicePlane,path)==KERN_SUCCESS)d.path=path;
                if(d.leaf_candidate() && d.guid!=0){result.push_back({std::move(d),entry,Stack::asfw});kept=true;}
            }
            if(unit)IOObjectRelease(unit);
            if(pci)IOObjectRelease(pci);
        }
        if(controller)IOObjectRelease(controller);
        if(!kept)IOObjectRelease(entry);
    }
    IOObjectRelease(iterator);return result;
}
std::vector<Candidate> candidates() {
    if(__builtin_available(macOS 26.0,*))return asfw_candidates();
    return apple_candidates();
}
// Current ASFWBusGeneration of the unit nub with this registry ID; 0 if gone.
std::uint32_t asfw_generation(std::uint64_t unit_id) {
    auto unit=IOServiceGetMatchingService(kIOMasterPortDefault,IORegistryEntryIDMatching(unit_id));
    if(!unit)return 0;
    const auto value=has_number(unit,CFSTR("ASFWBusGeneration"))?static_cast<std::uint32_t>(number(unit,CFSTR("ASFWBusGeneration"))):0;
    IOObjectRelease(unit);return value;
}
void release_candidates(std::vector<Candidate>& list){for(auto& c:list)IOObjectRelease(c.nub);list.clear();}
} // namespace
struct ScsiTransport::Impl {
    IOCFPlugInInterface **plugin{},**fw_plugin{};
    SCSITaskDeviceInterface** device{};
    IOFireWireLibDeviceRef fw{};
    SCSITaskInterface** task{};
    Bytes dma;
    std::unique_ptr<void,decltype(&std::free)> aligned{nullptr,&std::free};
    bool exclusive{};
    Stack stack{Stack::apple};
    std::uint64_t asfw_unit{};
    std::uint32_t bus_generation{};
    std::thread::id owner{std::this_thread::get_id()};
    ReferenceCommandGate gate{
#ifdef VALEO_EXPERIMENTAL_CAPTURE
        true
#else
        false
#endif
        ,61440,
#ifdef VALEO_EXPERIMENTAL_AUXILIARY
        true
#else
        false
#endif
    };
    void same_thread(){if(owner!=std::this_thread::get_id())throw Error(ErrorCode::invalid,"SCSI session used from another thread");}
    std::uint32_t read_generation() {
        if(stack==Stack::asfw) {
            const auto current=asfw_generation(asfw_unit);
            if(!current)throw Error(ErrorCode::disconnected,"ASFW SBP-2 unit disappeared");
            return current;
        }
        UInt32 current=0;check((*fw)->GetBusGeneration(fw,&current),"read FireWire bus generation");return current;
    }
    void check_generation() {
        if(read_generation()!=bus_generation)throw Error(ErrorCode::stale_generation,"FireWire bus reset; discard session and re-identify");
    }
};
ScsiTransport::ScsiTransport():impl_(new Impl){}
ScsiTransport::~ScsiTransport(){close();}
std::vector<Device> ScsiTransport::enumerate() {
    impl_->same_thread();auto list=candidates();std::vector<Device> result;
    for(auto& c:list)result.push_back(std::move(c.device));release_candidates(list);return result;
}
void ScsiTransport::open(std::uint64_t registry_id) {
    auto& s=*impl_;s.same_thread();if(s.device)throw Error(ErrorCode::busy,"SCSI session already open");
    auto list=candidates();auto found=std::find_if(list.begin(),list.end(),[&](const auto& c){return c.device.registry_id==registry_id;});
    if(found==list.end()){release_candidates(list);throw Error(ErrorCode::unsupported,"No exact Leaf SCSI candidate for this registry ID");}
    const auto nub=found->nub;IOObjectRetain(nub);const auto stack=found->stack;release_candidates(list);
    io_registry_entry_t fw_service=0;
    try {
        SInt32 score=0;
        check(IOCreatePlugInInterfaceForService(nub,kIOSCSITaskDeviceUserClientTypeID,kIOCFPlugInInterfaceID,&s.plugin,&score),"create SCSI task plugin");
        if((*s.plugin)->QueryInterface(s.plugin,CFUUIDGetUUIDBytes(kIOSCSITaskDeviceInterfaceID),reinterpret_cast<void**>(&s.device)) || !s.device)
            throw Error(ErrorCode::unsupported,"SCSI device interface unavailable");
        s.stack=stack;
        if(stack==Stack::asfw) {
            // No IOFireWireLib under ASFW: the generation comes from the unit
            // nub's ASFWBusGeneration, refreshed by ASFW on every resume.
            s.asfw_unit=registry_id;
            check((*s.device)->ObtainExclusiveAccess(s.device),"obtain SCSI exclusive access");s.exclusive=true;
            s.bus_generation=asfw_generation(registry_id);
            if(!s.bus_generation)throw Error(ErrorCode::disconnected,"ASFW SBP-2 unit disappeared");
            IOObjectRelease(nub);
            return;
        }
        fw_service=ancestor(nub,"IOFireWireDevice");if(!fw_service)throw Error(ErrorCode::disconnected,"FireWire device disappeared");
        check(IOCreatePlugInInterfaceForService(fw_service,kIOFireWireLibTypeID,kIOCFPlugInInterfaceID,&s.fw_plugin,&score),"create read-only FireWire generation interface");
        if((*s.fw_plugin)->QueryInterface(s.fw_plugin,CFUUIDGetUUIDBytes(kIOFireWireDeviceInterfaceID_v4),reinterpret_cast<void**>(&s.fw)) || !s.fw)
            throw Error(ErrorCode::unsupported,"FireWire generation interface unavailable");
        // GetBusGeneration is a read-only query; do NOT exclusively Open the
        // FireWire device, whose command transport is owned by the SCSI kernel.
        check((*s.device)->ObtainExclusiveAccess(s.device),"obtain SCSI exclusive access");s.exclusive=true;
        check((*s.fw)->GetBusGeneration(s.fw,&s.bus_generation),"read initial FireWire generation");
        IOObjectRelease(fw_service);IOObjectRelease(nub);
    }catch(...){if(fw_service)IOObjectRelease(fw_service);IOObjectRelease(nub);close();throw;}
}
Reply ScsiTransport::submit(const Command& command) {
    auto& s=*impl_;s.same_thread();command.validate();
    if(!s.exclusive)throw Error(ErrorCode::disconnected,"No exclusive SCSI session");
#ifndef VALEO_EXPERIMENTAL_READS
    throw Error(ErrorCode::unsupported,"Camera commands disabled in this build");
#else
    if(command.generation!=s.bus_generation)throw Error(ErrorCode::stale_generation,"Command belongs to an old session generation");
    if(!s.gate.allows(command))throw Error(ErrorCode::unsupported,"Command outside reviewed SCSI session allowlist");
    if(s.task)throw Error(ErrorCode::busy,"SCSI task already active");
    const bool inbound=command.direction==Direction::from_device;
    try {
        using Clock=std::chrono::steady_clock;
        const auto prepared=Clock::now();
        s.check_generation();s.dma=inbound?Bytes(command.incoming_size,0):command.outgoing;
        void* memory=nullptr;
        const auto page=::sysconf(_SC_PAGESIZE);
        if(page<1 || ::posix_memalign(&memory,static_cast<std::size_t>(page),s.dma.size())!=0)
            throw Error(ErrorCode::io,"Cannot allocate page-aligned SCSI buffer");
        s.aligned.reset(memory);std::memcpy(memory,s.dma.data(),s.dma.size());
        s.task=(*s.device)->CreateSCSITask(s.device);if(!s.task)throw Error(ErrorCode::io,"Create SCSI task failed");
        auto cdb=command.cdb;check((*s.task)->SetCommandDescriptorBlock(s.task,cdb.data(),6),"set Leaf six-byte CDB");
        SCSITaskSGElement range{reinterpret_cast<UInt64>(s.aligned.get()),static_cast<IOByteCount>(s.dma.size())};
        check((*s.task)->SetScatterGatherEntries(s.task,&range,1,s.dma.size(),inbound?kSCSIDataTransfer_FromTargetToInitiator:kSCSIDataTransfer_FromInitiatorToTarget),"map owned SCSI buffer");
        check((*s.task)->SetTimeoutDuration(s.task,static_cast<UInt32>(command.timeout.count())),"set SCSI timeout");
        SCSI_Sense_Data sense{};SCSITaskStatus status=kSCSITaskStatus_No_Status;UInt64 transferred=0;
        const auto executing=Clock::now();
        const auto result=(*s.task)->ExecuteTaskSync(s.task,&sense,&status,&transferred);
        const auto completed=Clock::now();
        if((*s.task)->IsTaskActive(s.task))throw Error(ErrorCode::io,"Synchronous SCSI task unexpectedly still active");
        check(result,"execute SCSI task");s.check_generation();
        if(inbound)std::memcpy(s.dma.data(),s.aligned.get(),s.dma.size());
        SCSIServiceResponse response{};check((*s.task)->GetSCSIServiceResponse(s.task,&response),"read SCSI completion");
        if(status!=kSCSITaskStatus_GOOD || response!=kSCSIServiceResponse_TASK_COMPLETE || transferred!=s.dma.size()) {
            std::ostringstream message;message<<"SCSI completion op=0x"<<std::hex<<unsigned(command.cdb[1])<<std::dec
                <<" status="<<unsigned(status)<<" response="<<unsigned(response)<<" transferred="<<transferred<<" expected="<<s.dma.size()
                <<" sense=";
            const auto raw=reinterpret_cast<const std::uint8_t*>(&sense);
            for(std::size_t i=0;i<std::min<std::size_t>(18,sizeof(sense));++i)message<<std::hex<<unsigned(raw[i])<<',';
            const bool timeout=response==kSCSIServiceResponse_SERVICE_DELIVERY_OR_TARGET_FAILURE &&
                (status==kSCSITaskStatus_TaskTimeoutOccurred || status==kSCSITaskStatus_ProtocolTimeoutOccurred);
            throw Error(timeout?ErrorCode::timeout:ErrorCode::protocol,message.str());
        }
        (*s.task)->Release(s.task);s.task=nullptr;
        s.gate.completed(command,inbound?s.dma:Bytes{});
        return {inbound?std::move(s.dma):Bytes{},s.bus_generation,0,0,
            std::chrono::duration<double>(executing-prepared).count(),
            std::chrono::duration<double>(completed-executing).count(),
            std::chrono::duration<double>(Clock::now()-completed).count()};
    }catch(...){close();throw;}
#endif
}
void ScsiTransport::close() noexcept {
    auto& s=*impl_;
    if(s.task && (*s.task)->IsTaskActive(s.task))(*s.task)->AbortTask(s.task);
    if(s.device && s.exclusive)(*s.device)->ReleaseExclusiveAccess(s.device);
    s.exclusive=false;
    if(s.task){(*s.task)->Release(s.task);s.task=nullptr;}
    if(s.device){(*s.device)->Release(s.device);s.device=nullptr;}
    // Retain DMA storage until Stop/clientClose has flushed outstanding tasks.
    if(s.plugin){IODestroyPlugInInterface(s.plugin);s.plugin=nullptr;}
    if(s.fw){(*s.fw)->Release(s.fw);s.fw=nullptr;}
    if(s.fw_plugin){IODestroyPlugInInterface(s.fw_plugin);s.fw_plugin=nullptr;}
    s.aligned.reset();s.dma.clear();s.bus_generation=0;s.asfw_unit=0;s.stack=Stack::apple;s.gate.reset();
}
std::uint32_t ScsiTransport::generation() const noexcept {
    const auto& s=*impl_;if(!s.exclusive)return 0;
    if(s.stack==Stack::asfw)return asfw_generation(s.asfw_unit);
    if(!s.fw)return 0;
    UInt32 current=0;return (*s.fw)->GetBusGeneration(s.fw,&current)==kIOReturnSuccess?current:0;
}
bool ScsiTransport::paired_capture_enabled() const noexcept {
#if defined(VALEO_EXPERIMENTAL_READS) && defined(VALEO_EXPERIMENTAL_CAPTURE) && defined(VALEO_EXPERIMENTAL_AUXILIARY)
    return true;
#else
    return false;
#endif
}
} // namespace valeo
