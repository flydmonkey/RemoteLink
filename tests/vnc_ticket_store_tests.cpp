#include "remotelink/vnc_ticket_store.hpp"

#include <cassert>
#include <chrono>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    remotelink::VncTicketStore tickets(50ms);
    const auto ticket = tickets.issue({.target_id="desktop-1", .hostname="127.0.0.1", .port=5901,
        .enable_clipboard=false, .show_wallpaper=false, .font_smoothing=true,
        .full_window_drag=false, .menu_animations=true, .desktop_composition=false,
        .dpi=144, .image_format="png", .resize_method="reconnect"});
    assert(ticket.size() == 64);
    assert(tickets.size() == 1);
    const auto destination = tickets.consume(ticket);
    assert(destination.has_value());
    assert(destination->target_id == "desktop-1");
    assert(destination->hostname == "127.0.0.1");
    assert(destination->port == 5901);
    assert(!destination->enable_clipboard);
    assert(!destination->show_wallpaper);
    assert(destination->font_smoothing);
    assert(!destination->full_window_drag);
    assert(destination->menu_animations);
    assert(!destination->desktop_composition);
    assert(destination->dpi == 144);
    assert(destination->image_format == "png");
    assert(destination->resize_method == "reconnect");
    assert(!tickets.consume(ticket).has_value());
    const auto expired = tickets.issue({.target_id="desktop-2", .hostname="127.0.0.1", .port=5902});
    std::this_thread::sleep_for(70ms);
    assert(!tickets.consume(expired).has_value());
    return 0;
}
