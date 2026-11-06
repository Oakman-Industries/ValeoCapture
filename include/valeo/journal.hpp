// Copyright (c) 2026 Oakman Industries. Licensed under CC BY-NC-SA 4.0
#pragma once
#include "transport.hpp"
#include <string>
#ifndef VALEO_REFERENCE_NO_FILESYSTEM
#include <filesystem>
#endif

namespace valeo {
// File exists before bytes are acknowledged; incomplete captures stay as .partial.
class Journal {
    int fd_{-1};
    std::string partial_, final_;
    std::uint64_t expected_{}, written_{};
public:
    Journal(const std::string& final, std::uint64_t expected);
#ifndef VALEO_REFERENCE_NO_FILESYSTEM
    Journal(const std::filesystem::path& final,std::uint64_t expected):Journal(final.string(),expected){}
#endif
    ~Journal();
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;
    void append(const Bytes&);
    void commit();
    std::uint64_t written() const noexcept { return written_; }
};
} // namespace valeo
