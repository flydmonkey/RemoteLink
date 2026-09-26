#include "remotelink/vnc_bridge.hpp"

extern "C" {
#include <rfb/rfb.h>
#include <rfb/rfbclient.h>
}

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

namespace remotelink {
namespace {
void bridge_tag() {}
}

struct VncBridge::Impl {
    VncBridgeOptions options;
    rfbClient* upstream = nullptr;
    rfbScreenInfoPtr downstream = nullptr;
    std::atomic_bool stopping = false;
    std::atomic_bool cleaned = false;
    std::mutex upstream_mutex;
    std::jthread worker;

    ~Impl() { shutdown(); }

    static Impl* from(rfbClient* client) {
        return static_cast<Impl*>(rfbClientGetClientData(client,
            reinterpret_cast<void*>(bridge_tag)));
    }
    static char* password(rfbClient* client) {
        return ::strdup(from(client)->options.password.c_str());
    }
    static rfbCredential* credential(rfbClient* client, int type) {
        const auto self = from(client);
        auto* value = static_cast<rfbCredential*>(std::calloc(1, sizeof(rfbCredential)));
        if (type == rfbCredentialTypeUser) {
            value->userCredential.username = ::strdup(self->options.username.c_str());
            value->userCredential.password = ::strdup(self->options.password.c_str());
        } else if (type == rfbCredentialTypeX509) {
            value->x509Credential.x509CACertFile = self->options.ca_file.empty()
                ? nullptr : ::strdup(self->options.ca_file.c_str());
            value->x509Credential.x509CrlVerifyMode = rfbX509CrlVerifyNone;
        }
        return value;
    }
    static rfbBool resize(rfbClient* client) {
        const auto size = static_cast<std::size_t>(client->width) * client->height * 4;
        auto* buffer = static_cast<uint8_t*>(std::realloc(client->frameBuffer, size));
        if (!buffer) return FALSE;
        client->frameBuffer = buffer;
        const auto self = from(client);
        if (self->downstream)
            rfbNewFramebuffer(self->downstream, reinterpret_cast<char*>(buffer),
                client->width, client->height, 8, 3, 4);
        return TRUE;
    }
    static void updated(rfbClient* client, int x, int y, int width, int height) {
        const auto self = from(client);
        if (self->downstream) rfbMarkRectAsModified(self->downstream, x, y, x + width, y + height);
    }
    static void pointer(int mask, int x, int y, rfbClientPtr downstream_client) {
        const auto self = static_cast<Impl*>(downstream_client->screen->screenData);
        std::lock_guard lock(self->upstream_mutex);
        SendPointerEvent(self->upstream, x, y, mask);
    }
    static void key(rfbBool down, rfbKeySym symbol, rfbClientPtr downstream_client) {
        const auto self = static_cast<Impl*>(downstream_client->screen->screenData);
        std::lock_guard lock(self->upstream_mutex);
        SendKeyEvent(self->upstream, symbol, down);
    }
    static void cut_text(char* text, int length, rfbClientPtr downstream_client) {
        const auto self = static_cast<Impl*>(downstream_client->screen->screenData);
        std::lock_guard lock(self->upstream_mutex);
        SendClientCutText(self->upstream, text, length);
    }
    void run() {
        while (!stopping) {
            int ready = 0;
            {
                std::lock_guard lock(upstream_mutex);
                ready = WaitForMessage(upstream, 10000);
                if (ready > 0 && !HandleRFBServerMessage(upstream)) ready = -1;
            }
            if (ready < 0) break;
            rfbProcessEvents(downstream, 10000);
        }
        stopping = true;
    }
    void shutdown() {
        stopping = true;
        if (cleaned.exchange(true)) return;
        if (upstream && upstream->sock >= 0) ::shutdown(upstream->sock, SHUT_RDWR);
        if (downstream) rfbShutdownServer(downstream, TRUE);
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id()) worker.join();
        if (downstream) { rfbScreenCleanup(downstream); downstream = nullptr; }
        if (upstream) { rfbClientCleanup(upstream); upstream = nullptr; }
    }
};

VncBridge::VncBridge(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
VncBridge::~VncBridge() { stop(); }

std::shared_ptr<VncBridge> VncBridge::create(VncBridgeOptions options, std::string& error) {
    auto impl = std::make_unique<Impl>();
    impl->options = std::move(options);
    impl->upstream = rfbGetClient(8, 3, 4);
    if (!impl->upstream) { error = "unable to create VNC client"; return {}; }
    rfbClientSetClientData(impl->upstream, reinterpret_cast<void*>(bridge_tag), impl.get());
    impl->upstream->GetPassword = Impl::password;
    impl->upstream->GetCredential = Impl::credential;
    impl->upstream->MallocFrameBuffer = Impl::resize;
    impl->upstream->GotFrameBufferUpdate = Impl::updated;
    // Ask the source server to send cursor shapes separately instead of
    // compositing the pointer into the framebuffer. The browser uses its own
    // low-latency cursor, so the received shape itself is intentionally ignored.
    impl->upstream->appData.useRemoteCursor = TRUE;
    impl->upstream->canHandleNewFBSize = TRUE;
    const auto destination = impl->options.hostname + ":" + std::to_string(impl->options.port);
    char program[] = "remotelink-vnc";
    auto* target = ::strdup(destination.c_str());
    char* arguments[] = {program, target, nullptr};
    int count = 2;
    if (!rfbInitClient(impl->upstream, &count, arguments)) {
        // rfbInitClient() owns cleanup of the client when initialization fails.
        impl->upstream = nullptr;
        std::free(target); error = "VNC authentication or TLS negotiation failed"; return {};
    }
    std::free(target);
    int argc = 0;
    impl->downstream = rfbGetScreen(&argc, nullptr, impl->upstream->width,
        impl->upstream->height, 8, 3, 4);
    if (!impl->downstream) { error = "unable to create local VNC bridge"; return {}; }
    impl->downstream->screenData = impl.get();
    impl->downstream->frameBuffer = reinterpret_cast<char*>(impl->upstream->frameBuffer);
    char empty_cursor[] = " ";
    char empty_mask[] = " ";
    impl->downstream->cursor = rfbMakeXCursor(1, 1, empty_cursor, empty_mask);
    impl->downstream->alwaysShared = TRUE;
    impl->downstream->autoPort = TRUE;
    impl->downstream->port = 5900;
    impl->downstream->listenInterface = htonl(INADDR_LOOPBACK);
    impl->downstream->ptrAddEvent = Impl::pointer;
    impl->downstream->kbdAddEvent = Impl::key;
    impl->downstream->setXCutText = Impl::cut_text;
    rfbInitServer(impl->downstream);
    if (impl->downstream->listenSock < 0) { error = "unable to listen for local VNC bridge"; return {}; }
    auto bridge = std::shared_ptr<VncBridge>(new VncBridge(std::move(impl)));
    bridge->impl_->worker = std::jthread([state=bridge->impl_.get()] { state->run(); });
    return bridge;
}

std::uint16_t VncBridge::port() const { return static_cast<std::uint16_t>(impl_->downstream->port); }
void VncBridge::stop() { if (impl_) impl_->shutdown(); }

}  // namespace remotelink
