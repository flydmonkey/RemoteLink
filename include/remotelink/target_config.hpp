#pragma once

#include "remotelink/rdp_frame_source.hpp"

#include <string>
#include <vector>

namespace remotelink {

struct TargetConfig {
    std::string id;
    std::string name;
    std::string group;
    RdpConnectionOptions rdp;
};

std::vector<TargetConfig> load_targets(const std::string& path,
                                       const std::string& allowed_hosts);

}  // namespace remotelink
