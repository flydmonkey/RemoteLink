#include <rtc/rtc.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;
using json = nlohmann::json;

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::cerr << "usage: remotelink_webrtc_smoke ACCESS_TOKEN [TARGET_ID]\n";
        return 2;
    }

    rtc::InitLogger(rtc::LogLevel::Warning);
    std::atomic_bool authenticated = false;
    std::string target_id = argc == 3 ? argv[2] : "";
    std::atomic_bool connected = false;
    std::atomic_bool input_sent = false;
    std::atomic_uint64_t rtp_packets = 0;

    rtc::WebSocket::Configuration web_socket_config;
    web_socket_config.disableTlsVerification = true;
    auto socket = std::make_shared<rtc::WebSocket>(std::move(web_socket_config));
    std::shared_ptr<rtc::PeerConnection> peer;
    std::shared_ptr<rtc::Track> track;
    std::shared_ptr<rtc::DataChannel> input;

    socket->onOpen([socket, token = std::string(argv[1])] {
        socket->send(json{{"type", "authenticate"}, {"token", token}}.dump());
    });
    socket->onMessage([&](rtc::message_variant value) {
        if (!std::holds_alternative<std::string>(value)) return;
        const auto message = json::parse(std::get<std::string>(value));
        const std::string type = message.value("type", "");
        if (type == "auth-error") {
            std::cerr << "authentication rejected\n";
            socket->close();
            return;
        }
        if (type == "authenticated") {
            authenticated = true;
            if (target_id.empty() && message.contains("targets") && !message["targets"].empty())
                target_id = message["targets"][0].value("id", "");
            rtc::Configuration configuration;
            configuration.disableAutoNegotiation = true;
            peer = std::make_shared<rtc::PeerConnection>(configuration);
            peer->onStateChange([&](rtc::PeerConnection::State state) {
                connected = state == rtc::PeerConnection::State::Connected;
            });
            peer->onLocalDescription([socket](rtc::Description description) {
                socket->send(json{{"type", description.typeString()},
                                  {"sdp", std::string(description)}}.dump());
            });
            peer->onLocalCandidate([socket](rtc::Candidate candidate) {
                socket->send(json{{"type", "candidate"},
                                  {"candidate", std::string(candidate)},
                                  {"mid", candidate.mid()}}.dump());
            });
            peer->onTrack([&](std::shared_ptr<rtc::Track> incoming) {
                track = std::move(incoming);
                track->onMessage([&](rtc::binary) { ++rtp_packets; }, nullptr);
            });
            peer->onDataChannel([&](std::shared_ptr<rtc::DataChannel> channel) {
                input = std::move(channel);
                input->onOpen([&] {
                    input->send(R"({"type":"pointer","x":0.5,"y":0.5,"buttons":0})");
                    input->send(R"({"type":"pointer-button","button":0,"down":true,"x":0.05,"y":0.5})");
                    input->send(R"({"type":"pointer-button","button":0,"down":false,"x":0.05,"y":0.5})");
                    input->send(R"({"type":"wheel","delta":1})");
                    input->send(R"({"type":"key","code":"ShiftLeft","key":"Shift","down":true})");
                    input->send(R"({"type":"key","code":"ShiftLeft","key":"Shift","down":false})");
                    input_sent = true;
                });
            });
            socket->send(json{{"type", "start"}, {"target", target_id}}.dump());
            return;
        }
        if (type == "offer" && peer) {
            peer->setRemoteDescription(
                rtc::Description(message.at("sdp").get<std::string>(), "offer"));
            peer->setLocalDescription(rtc::Description::Type::Answer);
            return;
        }
        if (type == "candidate" && peer) {
            peer->addRemoteCandidate(rtc::Candidate(
                message.at("candidate").get<std::string>(), message.value("mid", "0")));
        }
    });

    socket->open("wss://localhost:18080/ws");
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline &&
           !(authenticated && connected && input_sent && rtp_packets.load() >= 3)) {
        std::this_thread::sleep_for(100ms);
    }

    const bool passed = authenticated && connected && input_sent && rtp_packets.load() >= 3;
    if (input) input->close();
    if (track) track->close();
    if (peer) peer->close();
    socket->close();

    if (!passed) {
        std::cerr << "WebRTC smoke test failed: authenticated=" << authenticated
                  << " connected=" << connected << " input=" << input_sent
                  << " rtpPackets=" << rtp_packets << '\n';
        return 1;
    }
    std::cout << "WebRTC media and DataChannel smoke test passed; RTP packets="
              << rtp_packets << '\n';
    return 0;
}
