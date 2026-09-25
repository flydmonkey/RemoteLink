#include "remote_gateway/vnc_ticket_store.hpp"

#include <openssl/rand.h>

#include <array>
#include <stdexcept>

namespace remote_gateway {
namespace {
std::string random_ticket() {
    std::array<unsigned char, 32> bytes {};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1)
        throw std::runtime_error("unable to generate VNC session ticket");
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(hex[byte >> 4]);
        result.push_back(hex[byte & 0x0f]);
    }
    return result;
}
}  // namespace

VncTicketStore::VncTicketStore(std::chrono::milliseconds lifetime) : lifetime_(lifetime) {
    if (lifetime_ <= std::chrono::milliseconds::zero())
        throw std::invalid_argument("VNC ticket lifetime must be positive");
}

std::string VncTicketStore::issue(VncDestination destination) {
    if (destination.target_id.empty() || destination.hostname.empty() || destination.port == 0)
        throw std::invalid_argument("VNC destination is incomplete");
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    remove_expired_locked(now);
    std::string ticket;
    do ticket = random_ticket(); while (entries_.contains(ticket));
    destination.session_id = ticket;
    entries_.emplace(ticket, Entry {std::move(destination), now + lifetime_});
    return ticket;
}

std::optional<VncDestination> VncTicketStore::consume(const std::string& ticket) {
    std::lock_guard lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    remove_expired_locked(now);
    const auto found = entries_.find(ticket);
    if (found == entries_.end()) return std::nullopt;
    auto destination = std::move(found->second.destination);
    entries_.erase(found);
    return destination;
}

std::size_t VncTicketStore::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void VncTicketStore::remove_expired_locked(std::chrono::steady_clock::time_point now) {
    std::erase_if(entries_, [now](const auto& item) { return item.second.expires_at <= now; });
}

}  // namespace remote_gateway
