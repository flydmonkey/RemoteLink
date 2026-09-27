#include "remotelink/target_config.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
void set_password() {
#ifdef _WIN32
    _putenv_s("REMOTELINK_TEST_TARGET_PASSWORD", "secret");
#else
    setenv("REMOTELINK_TEST_TARGET_PASSWORD", "secret", 1);
#endif
}

bool rejected(const std::filesystem::path& path, const std::string& json,
              const std::string& allowed = "10.0.0.1") {
    std::ofstream(path) << json;
    try { (void)remotelink::load_targets(path.string(), allowed); }
    catch (const std::runtime_error&) { return true; }
    return false;
}
}  // namespace

int main() {
    set_password();
    const auto path = std::filesystem::temp_directory_path() / "remotelink-target-test.json";
    const std::string valid = R"({"targets":[{"id":"server-1","name":"Server 1","host":"10.0.0.1","username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})";
    std::ofstream(path) << valid;
    const auto targets = remotelink::load_targets(path.string(), "10.0.0.1");
    assert(targets.size() == 1);
    assert(targets[0].id == "server-1");
    assert(targets[0].rdp_backend == "freerdp");
    assert(targets[0].rdp.password == "secret");
    assert(targets[0].performance_preset == "balanced");
    assert(targets[0].rdp.video_bitrate == 4000000);
    assert(targets[0].rdp.redirect_clipboard);

    const std::string guacamole = R"({"targets":[{"id":"legacy-server","name":"Legacy Server","rdpBackend":"guacamole","host":"10.0.0.1","username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})";
    std::ofstream(path) << guacamole;
    const auto guacamole_targets = remotelink::load_targets(path.string(), "10.0.0.1");
    assert(guacamole_targets.size() == 1);
    assert(guacamole_targets[0].rdp_backend == "guacamole");

    const auto secret_path = std::filesystem::temp_directory_path() / "remotelink-target-secret.txt";
    std::ofstream(secret_path) << "file-secret\n";
    std::ofstream(path) << "{\"targets\":[{\"id\":\"file-secret\",\"name\":\"File Secret\",\"host\":\"10.0.0.1\",\"username\":\"admin\",\"passwordFile\":\"" << secret_path.string() << "\"}]}";
    const auto file_targets = remotelink::load_targets(path.string(), "10.0.0.1");
    assert(file_targets[0].rdp.password == "file-secret");

    std::ofstream(path) << R"({"targets":[]})";
    assert(remotelink::load_targets(path.string(), "*").empty());
    assert(rejected(path, R"({"targets":[{"id":"bad id","name":"Bad","host":"10.0.0.1","username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})"));
    assert(rejected(path, R"({"targets":[{"id":"a","name":"A","host":"10.0.0.2","username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})"));
    assert(rejected(path, R"({"targets":[{"id":"a","name":"A","host":"10.0.0.1","port":70000,"username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})"));
    assert(rejected(path, R"({"targets":[{"id":"a","name":"A","rdpBackend":"other","host":"10.0.0.1","username":"admin","passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})"));
    assert(rejected(path, R"({"targets":[{"id":"a","name":"A","host":"10.0.0.1","username":"admin","maxFps":100,"passwordEnv":"REMOTELINK_TEST_TARGET_PASSWORD"}]})"));
    std::filesystem::remove(path);
    std::filesystem::remove(secret_path);
    return 0;
}
