#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "remote_gateway/ssh_bridge.hpp"

namespace remote_gateway {

struct SshFileEntry {
    std::string name;
    std::string path;
    bool directory = false;
    std::uint64_t size = 0;
    std::uint64_t modified = 0;
};

struct SshDirectoryListing {
    std::string home;
    std::string path;
    std::vector<SshFileEntry> entries;
};

class SshFiles {
public:
    static bool list(const SshBridgeOptions& options, const std::string& path,
                     SshDirectoryListing& listing, std::string& error);
    static bool upload(const SshBridgeOptions& options, const std::string& path,
                       const std::string& data, std::string& error);
    static bool download(const SshBridgeOptions& options, const std::string& path,
                         std::string& data, std::string& error);
    static bool remove(const SshBridgeOptions& options, const std::string& path,
                       std::string& error);
};

}  // namespace remote_gateway
