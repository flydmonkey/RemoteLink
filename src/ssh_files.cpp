#include "remotelink/ssh_files.hpp"

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cstring>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>

namespace remotelink {
namespace {

int connect_tcp(const std::string& hostname, std::uint16_t port) {
    addrinfo hints{}; hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(hostname.c_str(), std::to_string(port).c_str(), &hints, &addresses) != 0) return -1;
    int socket_fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        socket_fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket_fd >= 0 && connect(socket_fd, address->ai_addr, address->ai_addrlen) == 0) break;
        if (socket_fd >= 0) close(socket_fd);
        socket_fd = -1;
    }
    freeaddrinfo(addresses);
    return socket_fd;
}

std::string session_error(LIBSSH2_SESSION* session, std::string fallback) {
    char* message = nullptr; int length = 0;
    libssh2_session_last_error(session, &message, &length, 0);
    return message && length > 0 ? std::string(message, static_cast<std::size_t>(length)) : fallback;
}

struct Session {
    int socket = -1;
    LIBSSH2_SESSION* ssh = nullptr;
    LIBSSH2_SFTP* sftp = nullptr;
    ~Session() {
        if (sftp) libssh2_sftp_shutdown(sftp);
        if (ssh) { libssh2_session_disconnect(ssh, "RemoteLink file operation complete"); libssh2_session_free(ssh); }
        if (socket >= 0) close(socket);
    }
};

std::unique_ptr<Session> open_session(const SshBridgeOptions& options, std::string& error) {
    static std::once_flag initialized;
    std::call_once(initialized, [] { libssh2_init(0); });
    auto state = std::make_unique<Session>();
    state->socket = connect_tcp(options.hostname, options.port);
    if (state->socket < 0) { error = "无法连接 SSH 目标"; return {}; }
    state->ssh = libssh2_session_init();
    if (!state->ssh || libssh2_session_handshake(state->ssh, state->socket) != 0) {
        error = state->ssh ? session_error(state->ssh, "SSH 握手失败") : "无法创建 SSH 会话"; return {};
    }
    const auto* fingerprint = reinterpret_cast<const unsigned char*>(
        libssh2_hostkey_hash(state->ssh, LIBSSH2_HOSTKEY_HASH_SHA256));
    std::ostringstream value; value << "SHA256:" << std::hex << std::setfill('0');
    if (!fingerprint) { error = "无法读取 SSH 主机指纹"; return {}; }
    for (int index = 0; index < 32; ++index) value << std::setw(2) << static_cast<int>(fingerprint[index]);
    if (options.host_key_sha256.empty() || options.host_key_sha256 != value.str()) {
        error = "SSH 主机指纹未确认或已变更"; return {};
    }
    int authenticated = -1;
    if (!options.private_key.empty()) {
        authenticated = libssh2_userauth_publickey_frommemory(state->ssh, options.username.c_str(),
            options.username.size(), nullptr, 0, options.private_key.c_str(), options.private_key.size(),
            options.passphrase.empty() ? nullptr : options.passphrase.c_str());
    } else {
        authenticated = libssh2_userauth_password_ex(state->ssh, options.username.c_str(),
            options.username.size(), options.password.c_str(), options.password.size(), nullptr);
    }
    if (authenticated != 0) { error = session_error(state->ssh, "SSH 身份验证失败"); return {}; }
    state->sftp = libssh2_sftp_init(state->ssh);
    if (!state->sftp) { error = session_error(state->ssh, "无法启动 SFTP"); return {}; }
    return state;
}

std::string home_directory(LIBSSH2_SFTP* sftp, std::string& error) {
    std::array<char, 4096> buffer{};
    const auto length = libssh2_sftp_realpath(sftp, ".", buffer.data(), buffer.size());
    if (length < 1) { error = "无法读取远端家目录"; return {}; }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

std::string resolve_path(LIBSSH2_SFTP* sftp, const std::string& requested, std::string& error) {
    const auto input = requested.empty() ? "." : requested;
    if (input.find('\0') != std::string::npos || input.size() > 4096) { error = "无效的远端路径"; return {}; }
    std::array<char, 4096> buffer{};
    const auto length = libssh2_sftp_realpath(sftp, input.c_str(), buffer.data(), buffer.size());
    if (length < 1) { error = "远端路径不存在"; return {}; }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

}  // namespace

bool SshFiles::list(const SshBridgeOptions& options, const std::string& path,
                    SshDirectoryListing& listing, std::string& error) {
    auto session = open_session(options, error); if (!session) return false;
    listing.home = home_directory(session->sftp, error); if (listing.home.empty()) return false;
    listing.path = resolve_path(session->sftp, path, error); if (listing.path.empty()) return false;
    LIBSSH2_SFTP_HANDLE* directory = libssh2_sftp_opendir(session->sftp, listing.path.c_str());
    if (!directory) { error = "无法打开远端目录"; return false; }
    std::array<char, 4096> name{}; LIBSSH2_SFTP_ATTRIBUTES attributes{};
    for (;;) {
        const auto length = libssh2_sftp_readdir_ex(directory, name.data(), name.size(), nullptr, 0, &attributes);
        if (length <= 0) break;
        std::string entry_name(name.data(), static_cast<std::size_t>(length));
        if (entry_name == "." || entry_name == "..") continue;
        const bool directory_entry = (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
            LIBSSH2_SFTP_S_ISDIR(attributes.permissions);
        listing.entries.push_back({entry_name,
            listing.path + (listing.path == "/" ? "" : "/") + entry_name, directory_entry,
            (attributes.flags & LIBSSH2_SFTP_ATTR_SIZE) ? attributes.filesize : 0,
            (attributes.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? attributes.mtime : 0});
        if (listing.entries.size() >= 5000) break;
    }
    libssh2_sftp_closedir(directory);
    return true;
}

bool SshFiles::upload(const SshBridgeOptions& options, const std::string& path,
                      const std::string& data, std::string& error) {
    auto session = open_session(options, error); if (!session) return false;
    if (path.empty() || path == "/" || path.find('\0') != std::string::npos) { error = "无效的远端路径"; return false; }
    auto* file = libssh2_sftp_open(session->sftp, path.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC, 0644);
    if (!file) { error = "无法创建远端文件"; return false; }
    std::size_t offset = 0;
    while (offset < data.size()) {
        const auto written = libssh2_sftp_write(file, data.data() + offset, data.size() - offset);
        if (written < 0) { libssh2_sftp_close(file); error = "写入远端文件失败"; return false; }
        offset += static_cast<std::size_t>(written);
    }
    libssh2_sftp_close(file); return true;
}

bool SshFiles::download(const SshBridgeOptions& options, const std::string& path,
                        std::string& data, std::string& error) {
    auto session = open_session(options, error); if (!session) return false;
    auto* file = libssh2_sftp_open(session->sftp, path.c_str(), LIBSSH2_FXF_READ, 0);
    if (!file) { error = "无法打开远端文件"; return false; }
    std::array<char, 65536> buffer{};
    for (;;) {
        const auto count = libssh2_sftp_read(file, buffer.data(), buffer.size());
        if (count == 0) break;
        if (count < 0) { libssh2_sftp_close(file); error = "读取远端文件失败"; return false; }
        if (data.size() + static_cast<std::size_t>(count) > 256ULL * 1024 * 1024) {
            libssh2_sftp_close(file); error = "文件超过 256 MB 下载限制"; return false;
        }
        data.append(buffer.data(), static_cast<std::size_t>(count));
    }
    libssh2_sftp_close(file); return true;
}

bool SshFiles::remove(const SshBridgeOptions& options, const std::string& path, std::string& error) {
    auto session = open_session(options, error); if (!session) return false;
    const auto resolved = resolve_path(session->sftp, path, error); if (resolved.empty()) return false;
    const auto home = home_directory(session->sftp, error);
    if (resolved == "/" || resolved == home) { error = "不能删除根目录或家目录"; return false; }
    LIBSSH2_SFTP_ATTRIBUTES attributes{};
    if (libssh2_sftp_stat(session->sftp, resolved.c_str(), &attributes) != 0) { error = "远端文件不存在"; return false; }
    const bool directory = (attributes.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
        LIBSSH2_SFTP_S_ISDIR(attributes.permissions);
    const int result = directory ? libssh2_sftp_rmdir(session->sftp, resolved.c_str())
                                 : libssh2_sftp_unlink(session->sftp, resolved.c_str());
    if (result != 0) { error = directory ? "目录不为空或无法删除" : "删除远端文件失败"; return false; }
    return true;
}

}  // namespace remotelink
