#include "remote_gateway/vnc_ticket_store.hpp"

#include <cassert>
#include <chrono>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    remote_gateway::VncTicketStore tickets(50ms);
    const auto ticket = tickets.issue({.target_id="desktop-1", .hostname="127.0.0.1", .port=5901});
    assert(ticket.size() == 64);
    assert(tickets.size() == 1);
    const auto destination = tickets.consume(ticket);
    assert(destination.has_value());
    assert(destination->target_id == "desktop-1");
    assert(destination->hostname == "127.0.0.1");
    assert(destination->port == 5901);
    assert(!tickets.consume(ticket).has_value());
    const auto expired = tickets.issue({.target_id="desktop-2", .hostname="127.0.0.1", .port=5902});
    std::this_thread::sleep_for(70ms);
    assert(!tickets.consume(expired).has_value());
    return 0;
}
