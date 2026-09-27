#pragma once

#include "remotelink/rdp_frame_source.hpp"

#include <string>
#include <vector>

namespace remotelink {

struct TargetConfig {
    std::string id;
    std::string name;
    std::string group;
    std::string rdp_backend = "freerdp";
    std::string performance_preset = "balanced";
    bool allow_audio = true;
    bool allow_printing = true;
    bool allow_files = true;
    std::string guacamole_image_format = "webp";
    std::string guacamole_resize_method = "display-update";
    std::uint32_t guacamole_dpi = 96;
    RdpConnectionOptions rdp;
};

std::vector<TargetConfig> load_targets(const std::string& path,
                                       const std::string& allowed_hosts);

}  // namespace remotelink
