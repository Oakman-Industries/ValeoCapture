// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#include "valeo/journal.hpp"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <system_error>
#include <sys/stat.h>

namespace valeo {
static void io_error(const char* action) { throw Error(ErrorCode::io,std::string(action)+": "+std::generic_category().message(errno)); }
Journal::Journal(const std::string& final,std::uint64_t expected) : partial_(final+".partial"),final_(final),expected_(expected) {
    if(!expected || expected>512ull*1024*1024) throw Error(ErrorCode::invalid,"Capture size must be validated and no larger than 512 MiB");
    struct stat existing{};
    if(::lstat(final.c_str(),&existing)==0)throw Error(ErrorCode::io,"Capture destination already exists");
    if(errno!=ENOENT)io_error("inspect capture destination");
    fd_=::open(partial_.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd_<0)io_error("create capture journal");
}
Journal::~Journal() { if(fd_>=0)::close(fd_); }
void Journal::append(const Bytes& b) {
    if(fd_<0 || b.size()>expected_-written_)throw Error(ErrorCode::invalid,"Capture exceeds expected size or is already committed");
    std::size_t offset=0;
    while(offset<b.size()) {auto n=::write(fd_,b.data()+offset,b.size()-offset);if(n<0){if(errno==EINTR)continue;io_error("write capture journal");}if(n==0)throw Error(ErrorCode::io,"Short journal write");offset+=static_cast<std::size_t>(n);written_+=static_cast<std::uint64_t>(n);}
    if(::fsync(fd_)!=0)io_error("sync capture journal");
}
void Journal::commit() {
    if(fd_<0 || written_!=expected_)throw Error(ErrorCode::invalid,"Cannot commit incomplete capture");
#ifdef __APPLE__
    if(::fcntl(fd_,F_FULLFSYNC)!=0)io_error("flush capture to durable storage");
#else
    if(::fsync(fd_)!=0)io_error("flush capture");
#endif
    // Hard link gives atomic publication without replacing another capture.
    if(::link(partial_.c_str(),final_.c_str())!=0)io_error("publish capture without overwrite");
    const auto slash=final_.find_last_of('/');
    const auto parent=slash==std::string::npos?std::string("."):slash==0?std::string("/"):final_.substr(0,slash);
    const int directory=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);
    if(directory<0)io_error("open capture directory");
    const int result=::fsync(directory);const int saved=errno;::close(directory);errno=saved;
    if(result!=0)io_error("sync capture directory");
    // If unlink fails the complete file is valid; retain .partial for inspection.
    if(::unlink(partial_.c_str())!=0)io_error("remove committed journal alias");
    ::close(fd_);fd_=-1;
}
} // namespace valeo
