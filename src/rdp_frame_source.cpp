#include "remotelink/rdp_frame_source.hpp"
#include "remotelink/input_text.hpp"
#include "remotelink/reconnect_policy.hpp"

#include <freerdp/freerdp.h>
#include <freerdp/addin.h>
#include <freerdp/client.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/rdpsnd.h>
#include <freerdp/codec/color.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/graphics.h>
#include <freerdp/input.h>
#include <freerdp/pointer.h>
#include <freerdp/scancode.h>
#include <freerdp/settings.h>
#include <freerdp/settings_keys.h>
#include <nlohmann/json.hpp>
#include <winpr/synch.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace remotelink {

namespace {
std::string encode_base64(const std::vector<std::uint8_t>& bytes);
}

void RdpFrameSource::set_status_handler(StatusHandler handler) {
    status_handler_ = std::move(handler);
}

void RdpFrameSource::set_cursor_handler(CursorHandler handler) {
    cursor_handler_ = std::move(handler);
}

void RdpFrameSource::set_audio_handler(AudioHandler handler) {
    audio_handler_ = std::move(handler);
}

void RdpFrameSource::publish_audio(const std::uint8_t* data, std::size_t size,
                                   std::uint32_t sample_rate, std::uint16_t channels) {
    if (audio_handler_) audio_handler_(data, size, sample_rate, channels);
}

void RdpFrameSource::publish_cursor(std::uint32_t width, std::uint32_t height,
                                    std::uint32_t hotspot_x, std::uint32_t hotspot_y,
                                    const std::vector<std::uint8_t>& rgba) {
    const std::string message = nlohmann::json{{"type", "cursor"}, {"width", width},
        {"height", height}, {"hotspotX", hotspot_x}, {"hotspotY", hotspot_y},
        {"pixels", encode_base64(rgba)}}.dump();
    publish_cursor_message(message);
}

void RdpFrameSource::publish_system_cursor(bool hidden) {
    publish_cursor_message(nlohmann::json{{"type", "cursor-system"},
                                          {"hidden", hidden}}.dump());
}

void RdpFrameSource::publish_cursor_message(const std::string& message) {
    if (cursor_handler_) cursor_handler_(message);
}
namespace {

struct GatewayRdpContext {
    rdpClientContext client;
    RdpFrameSource* owner = nullptr;
};
GatewayRdpContext* gateway_context(rdpContext* context);

struct GatewayAudioDevice {
    rdpsndDevicePlugin device {};
    RdpFrameSource* owner = nullptr;
    std::uint32_t sample_rate = 48000;
    std::uint16_t channels = 2;
};

FREERDP_LOAD_CHANNEL_ADDIN_ENTRY_FN fallback_addin_provider = nullptr;

BOOL audio_format_supported(rdpsndDevicePlugin*, const AUDIO_FORMAT* format) {
    return format != nullptr && format->wFormatTag == WAVE_FORMAT_PCM &&
           format->nSamplesPerSec >= 8000 && format->nSamplesPerSec <= 48000 &&
           (format->nChannels == 1 || format->nChannels == 2) &&
           format->wBitsPerSample == 16;
}

BOOL audio_open(rdpsndDevicePlugin* device, const AUDIO_FORMAT* format, UINT32) {
    if (!audio_format_supported(device, format)) return FALSE;
    auto* gateway = reinterpret_cast<GatewayAudioDevice*>(device);
    gateway->sample_rate = format->nSamplesPerSec;
    gateway->channels = format->nChannels;
    return TRUE;
}

UINT32 audio_get_volume(rdpsndDevicePlugin*) { return 0xFFFFFFFFU; }
BOOL audio_set_volume(rdpsndDevicePlugin*, UINT32) { return TRUE; }
void audio_close(rdpsndDevicePlugin*) {}
void audio_free(rdpsndDevicePlugin* device) {
    delete reinterpret_cast<GatewayAudioDevice*>(device);
}
UINT audio_play(rdpsndDevicePlugin* device, const BYTE* data, size_t size) {
    auto* gateway = reinterpret_cast<GatewayAudioDevice*>(device);
    if (gateway->owner != nullptr && data != nullptr && size != 0) {
        gateway->owner->publish_audio(data, size, gateway->sample_rate, gateway->channels);
    }
    return CHANNEL_RC_OK;
}

UINT VCAPITYPE gateway_rdpsnd_entry(PFREERDP_RDPSND_DEVICE_ENTRY_POINTS entry) {
    if (entry == nullptr || entry->rdpsnd == nullptr ||
        entry->pRegisterRdpsndDevice == nullptr) return ERROR_BAD_ARGUMENTS;
    auto* context = freerdp_rdpsnd_get_context(entry->rdpsnd);
    if (context == nullptr) return ERROR_INVALID_HANDLE;
    auto* audio = new (std::nothrow) GatewayAudioDevice;
    if (audio == nullptr) return CHANNEL_RC_NO_MEMORY;
    audio->owner = gateway_context(context)->owner;
    audio->device.FormatSupported = audio_format_supported;
    audio->device.Open = audio_open;
    audio->device.GetVolume = audio_get_volume;
    audio->device.SetVolume = audio_set_volume;
    audio->device.Play = audio_play;
    audio->device.Close = audio_close;
    audio->device.Free = audio_free;
    entry->pRegisterRdpsndDevice(entry->rdpsnd, &audio->device);
    return CHANNEL_RC_OK;
}

PVIRTUALCHANNELENTRY gateway_addin_provider(LPCSTR name, LPCSTR subsystem,
                                             LPCSTR type, DWORD flags) {
    if (name != nullptr && subsystem != nullptr &&
        std::strcmp(name, RDPSND_CHANNEL_NAME) == 0 &&
        std::strcmp(subsystem, "gateway") == 0) {
        return reinterpret_cast<PVIRTUALCHANNELENTRY>(gateway_rdpsnd_entry);
    }
    return fallback_addin_provider
        ? fallback_addin_provider(name, subsystem, type, flags) : nullptr;
}

GatewayRdpContext* gateway_context(rdpContext* context) {
    return reinterpret_cast<GatewayRdpContext*>(context);
}

BOOL pre_connect(freerdp*) { return TRUE; }

std::string encode_base64(const std::vector<std::uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((bytes.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t value = static_cast<std::uint32_t>(bytes[index]) << 16 |
            (index + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[index + 1]) << 8 : 0) |
            (index + 2 < bytes.size() ? bytes[index + 2] : 0);
        result.push_back(alphabet[(value >> 18) & 63]);
        result.push_back(alphabet[(value >> 12) & 63]);
        result.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        result.push_back(index + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return result;
}

BOOL publish_pointer(rdpContext* context, UINT16 cache_index, UINT16 hotspot_x,
                     UINT16 hotspot_y, UINT16 width, UINT16 height,
                     const BYTE* xor_mask, UINT32 xor_length,
                     const BYTE* and_mask, UINT32 and_length, UINT32 xor_bpp) {
    if (width == 0 || height == 0 || width > 256 || height > 256 ||
        xor_mask == nullptr || (xor_bpp != 15 && xor_bpp != 16 &&
        xor_bpp != 24 && xor_bpp != 32)) return TRUE;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    const auto* palette = context->gdi ? &context->gdi->palette : nullptr;
    if (!freerdp_image_copy_from_pointer_data(
            rgba.data(), PIXEL_FORMAT_RGBA32, width * 4, 0, 0, width, height,
            xor_mask, xor_length, and_mask, and_length, xor_bpp, palette)) return TRUE;
    auto* gateway = gateway_context(context);
    const std::string message = nlohmann::json{{"type", "cursor"}, {"width", width},
        {"height", height}, {"hotspotX", hotspot_x}, {"hotspotY", hotspot_y},
        {"pixels", encode_base64(rgba)}}.dump();
    gateway->owner->publish_cursor_message(message);
    return TRUE;
}

BOOL pointer_system(rdpContext* context, const POINTER_SYSTEM_UPDATE* pointer) {
    gateway_context(context)->owner->publish_system_cursor(pointer->type == SYSPTR_NULL);
    return TRUE;
}

BOOL pointer_color(rdpContext* context, const POINTER_COLOR_UPDATE* pointer) {
    return publish_pointer(context, pointer->cacheIndex, pointer->hotSpotX, pointer->hotSpotY,
        pointer->width, pointer->height, pointer->xorMaskData, pointer->lengthXorMask,
        pointer->andMaskData, pointer->lengthAndMask, 24);
}

BOOL pointer_new(rdpContext* context, const POINTER_NEW_UPDATE* pointer) {
    const auto& color = pointer->colorPtrAttr;
    return publish_pointer(context, color.cacheIndex, color.hotSpotX, color.hotSpotY,
        color.width, color.height, color.xorMaskData, color.lengthXorMask,
        color.andMaskData, color.lengthAndMask, pointer->xorBpp);
}

BOOL pointer_large(rdpContext* context, const POINTER_LARGE_UPDATE* pointer) {
    return publish_pointer(context, pointer->cacheIndex, pointer->hotSpotX, pointer->hotSpotY,
        pointer->width, pointer->height, pointer->xorMaskData, pointer->lengthXorMask,
        pointer->andMaskData, pointer->lengthAndMask, pointer->xorBpp);
}

BOOL pointer_cached(rdpContext* context, const POINTER_CACHED_UPDATE* pointer) {
    (void)context;
    (void)pointer;
    return TRUE;
}

BOOL graphics_pointer_new(rdpContext*, rdpPointer*) { return TRUE; }
void graphics_pointer_free(rdpContext*, rdpPointer*) {}
BOOL graphics_pointer_set(rdpContext* context, rdpPointer* pointer) {
    return publish_pointer(context, 0, static_cast<UINT16>(pointer->xPos),
        static_cast<UINT16>(pointer->yPos), static_cast<UINT16>(pointer->width),
        static_cast<UINT16>(pointer->height), pointer->xorMaskData,
        pointer->lengthXorMask, pointer->andMaskData, pointer->lengthAndMask,
        pointer->xorBpp);
}
BOOL graphics_pointer_set_null(rdpContext* context) {
    gateway_context(context)->owner->publish_system_cursor(true);
    return TRUE;
}
BOOL graphics_pointer_set_default(rdpContext* context) {
    gateway_context(context)->owner->publish_system_cursor(false);
    return TRUE;
}
BOOL graphics_pointer_set_position(rdpContext*, UINT32, UINT32) { return TRUE; }

BOOL desktop_resize(rdpContext* context) {
    if (context == nullptr || context->gdi == nullptr || context->settings == nullptr) {
        return FALSE;
    }
    return gdi_resize(context->gdi,
        freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
        freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
}

BOOL post_connect(freerdp* instance) {
    if (!gdi_init(instance, PIXEL_FORMAT_BGRA32)) return FALSE;
    instance->context->update->DesktopResize = desktop_resize;
    rdpPointer pointer {};
    pointer.size = sizeof(rdpPointer);
    pointer.New = graphics_pointer_new;
    pointer.Free = graphics_pointer_free;
    pointer.Set = graphics_pointer_set;
    pointer.SetNull = graphics_pointer_set_null;
    pointer.SetDefault = graphics_pointer_set_default;
    pointer.SetPosition = graphics_pointer_set_position;
    graphics_register_pointer(instance->context->graphics, &pointer);
    return TRUE;
}

void post_disconnect(freerdp* instance) {
    if (instance->context->gdi != nullptr) gdi_free(instance);
}

void channel_connected(void* context, const ChannelConnectedEventArgs* event) {
    auto* gateway = gateway_context(static_cast<rdpContext*>(context));
    if (event != nullptr && event->name != nullptr &&
        std::strcmp(event->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
        // The generic client helper conditionally compiles its RDPGFX branch.
        // Initialize the software-GDI graphics pipeline explicitly so every
        // RDPGFX callback is installed before the channel starts delivering
        // ResetGraphics/SurfaceCommands PDUs on its worker thread.
        if (gateway->client.context.gdi != nullptr && event->pInterface != nullptr) {
            auto* gfx = static_cast<RdpgfxClientContext*>(event->pInterface);
            gdi_graphics_pipeline_init(gateway->client.context.gdi, gfx);
        }
        return;
    }
    freerdp_client_OnChannelConnectedEventHandler(&gateway->client, event);
}

void channel_disconnected(void* context, const ChannelDisconnectedEventArgs* event) {
    auto* gateway = gateway_context(static_cast<rdpContext*>(context));
    if (event != nullptr && event->name != nullptr &&
        std::strcmp(event->name, RDPGFX_DVC_CHANNEL_NAME) == 0) {
        if (gateway->client.context.gdi != nullptr && event->pInterface != nullptr) {
            gdi_graphics_pipeline_uninit(gateway->client.context.gdi,
                static_cast<RdpgfxClientContext*>(event->pInterface));
        }
        return;
    }
    freerdp_client_OnChannelDisconnectedEventHandler(&gateway->client, event);
}

BOOL client_new(freerdp* instance, rdpContext* context) {
    instance->PreConnect = pre_connect;
    instance->PostConnect = post_connect;
    instance->PostDisconnect = post_disconnect;
    return PubSub_SubscribeChannelConnected(context->pubSub, channel_connected) >= 0 &&
           PubSub_SubscribeChannelDisconnected(context->pubSub, channel_disconnected) >= 0;
}

void client_free(freerdp*, rdpContext* context) {
    if (context == nullptr || context->pubSub == nullptr) return;
    PubSub_UnsubscribeChannelConnected(context->pubSub, channel_connected);
    PubSub_UnsubscribeChannelDisconnected(context->pubSub, channel_disconnected);
}

std::uint32_t scancode_for(const std::string& code) {
    static const std::unordered_map<std::string, std::uint32_t> codes = {
        {"KeyA", RDP_SCANCODE_KEY_A}, {"KeyB", RDP_SCANCODE_KEY_B},
        {"KeyC", RDP_SCANCODE_KEY_C}, {"KeyD", RDP_SCANCODE_KEY_D},
        {"KeyE", RDP_SCANCODE_KEY_E}, {"KeyF", RDP_SCANCODE_KEY_F},
        {"KeyG", RDP_SCANCODE_KEY_G}, {"KeyH", RDP_SCANCODE_KEY_H},
        {"KeyI", RDP_SCANCODE_KEY_I}, {"KeyJ", RDP_SCANCODE_KEY_J},
        {"KeyK", RDP_SCANCODE_KEY_K}, {"KeyL", RDP_SCANCODE_KEY_L},
        {"KeyM", RDP_SCANCODE_KEY_M}, {"KeyN", RDP_SCANCODE_KEY_N},
        {"KeyO", RDP_SCANCODE_KEY_O}, {"KeyP", RDP_SCANCODE_KEY_P},
        {"KeyQ", RDP_SCANCODE_KEY_Q}, {"KeyR", RDP_SCANCODE_KEY_R},
        {"KeyS", RDP_SCANCODE_KEY_S}, {"KeyT", RDP_SCANCODE_KEY_T},
        {"KeyU", RDP_SCANCODE_KEY_U}, {"KeyV", RDP_SCANCODE_KEY_V},
        {"KeyW", RDP_SCANCODE_KEY_W}, {"KeyX", RDP_SCANCODE_KEY_X},
        {"KeyY", RDP_SCANCODE_KEY_Y}, {"KeyZ", RDP_SCANCODE_KEY_Z},
        {"Digit0", RDP_SCANCODE_KEY_0}, {"Digit1", RDP_SCANCODE_KEY_1},
        {"Digit2", RDP_SCANCODE_KEY_2}, {"Digit3", RDP_SCANCODE_KEY_3},
        {"Digit4", RDP_SCANCODE_KEY_4}, {"Digit5", RDP_SCANCODE_KEY_5},
        {"Digit6", RDP_SCANCODE_KEY_6}, {"Digit7", RDP_SCANCODE_KEY_7},
        {"Digit8", RDP_SCANCODE_KEY_8}, {"Digit9", RDP_SCANCODE_KEY_9},
        {"Enter", RDP_SCANCODE_RETURN}, {"Escape", RDP_SCANCODE_ESCAPE},
        {"Backspace", RDP_SCANCODE_BACKSPACE}, {"Tab", RDP_SCANCODE_TAB},
        {"Space", RDP_SCANCODE_SPACE}, {"ArrowLeft", RDP_SCANCODE_LEFT},
        {"ArrowRight", RDP_SCANCODE_RIGHT}, {"ArrowUp", RDP_SCANCODE_UP},
        {"ArrowDown", RDP_SCANCODE_DOWN}, {"Delete", RDP_SCANCODE_DELETE},
        {"Insert", RDP_SCANCODE_INSERT}, {"Home", RDP_SCANCODE_HOME},
        {"End", RDP_SCANCODE_END}, {"PageUp", RDP_SCANCODE_PRIOR},
        {"PageDown", RDP_SCANCODE_NEXT}, {"F1", RDP_SCANCODE_F1},
        {"F2", RDP_SCANCODE_F2}, {"F3", RDP_SCANCODE_F3},
        {"F4", RDP_SCANCODE_F4}, {"F5", RDP_SCANCODE_F5},
        {"F6", RDP_SCANCODE_F6}, {"F7", RDP_SCANCODE_F7},
        {"F8", RDP_SCANCODE_F8}, {"F9", RDP_SCANCODE_F9},
        {"F10", RDP_SCANCODE_F10}, {"F11", RDP_SCANCODE_F11},
        {"F12", RDP_SCANCODE_F12},
        {"F13", RDP_SCANCODE_F13}, {"F14", RDP_SCANCODE_F14},
        {"F15", RDP_SCANCODE_F15}, {"F16", RDP_SCANCODE_F16},
        {"F17", RDP_SCANCODE_F17}, {"F18", RDP_SCANCODE_F18},
        {"F19", RDP_SCANCODE_F19}, {"F20", RDP_SCANCODE_F20},
        {"F21", RDP_SCANCODE_F21}, {"F22", RDP_SCANCODE_F22},
        {"F23", RDP_SCANCODE_F23}, {"F24", RDP_SCANCODE_F24},
        {"Minus", RDP_SCANCODE_OEM_MINUS}, {"Equal", RDP_SCANCODE_OEM_PLUS},
        {"BracketLeft", RDP_SCANCODE_OEM_4}, {"BracketRight", RDP_SCANCODE_OEM_6},
        {"Semicolon", RDP_SCANCODE_OEM_1}, {"Quote", RDP_SCANCODE_OEM_7},
        {"Backquote", RDP_SCANCODE_OEM_3}, {"Backslash", RDP_SCANCODE_OEM_5},
        {"IntlBackslash", RDP_SCANCODE_OEM_102},
        {"Comma", RDP_SCANCODE_OEM_COMMA}, {"Period", RDP_SCANCODE_OEM_PERIOD},
        {"Slash", RDP_SCANCODE_OEM_2},
        {"Numpad0", RDP_SCANCODE_NUMPAD0}, {"Numpad1", RDP_SCANCODE_NUMPAD1},
        {"Numpad2", RDP_SCANCODE_NUMPAD2}, {"Numpad3", RDP_SCANCODE_NUMPAD3},
        {"Numpad4", RDP_SCANCODE_NUMPAD4}, {"Numpad5", RDP_SCANCODE_NUMPAD5},
        {"Numpad6", RDP_SCANCODE_NUMPAD6}, {"Numpad7", RDP_SCANCODE_NUMPAD7},
        {"Numpad8", RDP_SCANCODE_NUMPAD8}, {"Numpad9", RDP_SCANCODE_NUMPAD9},
        {"NumpadDecimal", RDP_SCANCODE_DECIMAL}, {"NumpadAdd", RDP_SCANCODE_ADD},
        {"NumpadSubtract", RDP_SCANCODE_SUBTRACT}, {"NumpadMultiply", RDP_SCANCODE_MULTIPLY},
        {"NumpadDivide", RDP_SCANCODE_DIVIDE}, {"NumpadEnter", RDP_SCANCODE_RETURN_KP},
        {"CapsLock", RDP_SCANCODE_CAPSLOCK}, {"NumLock", RDP_SCANCODE_NUMLOCK},
        {"ScrollLock", RDP_SCANCODE_SCROLLLOCK}, {"PrintScreen", RDP_SCANCODE_PRINTSCREEN},
        {"Pause", RDP_SCANCODE_PAUSE}, {"ContextMenu", RDP_SCANCODE_APPS},
        {"IntlRo", RDP_SCANCODE_ABNT_C1}, {"IntlYen", RDP_SCANCODE_BACKSLASH_JP},
        {"Convert", RDP_SCANCODE_CONVERT_JP}, {"NonConvert", RDP_SCANCODE_NONCONVERT_JP},
        {"KanaMode", RDP_SCANCODE_KANA_HANGUL}, {"Lang1", RDP_SCANCODE_HANGUL},
        {"Lang2", RDP_SCANCODE_HANJA},
        {"AudioVolumeMute", RDP_SCANCODE_VOLUME_MUTE},
        {"AudioVolumeDown", RDP_SCANCODE_VOLUME_DOWN},
        {"AudioVolumeUp", RDP_SCANCODE_VOLUME_UP},
        {"MediaTrackNext", RDP_SCANCODE_MEDIA_NEXT_TRACK},
        {"MediaTrackPrevious", RDP_SCANCODE_MEDIA_PREV_TRACK},
        {"MediaStop", RDP_SCANCODE_MEDIA_STOP},
        {"MediaPlayPause", RDP_SCANCODE_MEDIA_PLAY_PAUSE},
        {"BrowserBack", RDP_SCANCODE_BROWSER_BACK},
        {"BrowserForward", RDP_SCANCODE_BROWSER_FORWARD},
        {"BrowserRefresh", RDP_SCANCODE_BROWSER_REFRESH},
        {"BrowserStop", RDP_SCANCODE_BROWSER_STOP},
        {"BrowserSearch", RDP_SCANCODE_BROWSER_SEARCH},
        {"BrowserFavorites", RDP_SCANCODE_BROWSER_FAVORITES},
        {"BrowserHome", RDP_SCANCODE_BROWSER_HOME},
        {"LaunchMail", RDP_SCANCODE_LAUNCH_MAIL},
        {"LaunchMediaPlayer", RDP_SCANCODE_LAUNCH_MEDIA_SELECT},
        {"ControlLeft", RDP_SCANCODE_LCONTROL}, {"ControlRight", RDP_SCANCODE_RCONTROL},
        {"AltLeft", RDP_SCANCODE_LMENU}, {"AltRight", RDP_SCANCODE_RMENU},
        {"ShiftLeft", RDP_SCANCODE_LSHIFT}, {"ShiftRight", RDP_SCANCODE_RSHIFT},
        {"MetaLeft", RDP_SCANCODE_LWIN}, {"MetaRight", RDP_SCANCODE_RWIN}
    };
    const auto found = codes.find(code);
    return found == codes.end() ? 0 : found->second;
}

}  // namespace

RdpFrameSource::RdpFrameSource(RdpConnectionOptions options)
    : options_(std::move(options)) {
    if (options_.hostname.empty() || options_.username.empty()) {
        throw std::invalid_argument("RDP hostname and username are required");
    }
}

RdpFrameSource::~RdpFrameSource() = default;

void RdpFrameSource::enqueue_input(std::string json_event) {
    std::lock_guard lock(input_mutex_);
    if (input_events_.size() < 1024) input_events_.push_back(std::move(json_event));
}

std::uint64_t RdpFrameSource::processed_input_events() const noexcept {
    return processed_input_events_.load();
}

std::uint64_t RdpFrameSource::published_frames() const noexcept {
    return published_frames_.load();
}

void RdpFrameSource::publish_frame(rdpContext* context) {
    if (!on_frame_) return;

    /* RDPGFX may update or replace the GDI primary buffer from a dynamic
     * channel thread. Take the same update lock used by FreeRDP's GDI while
     * snapshotting the framebuffer. */
    rdp_update_lock(context->update);
    if (context->gdi == nullptr || context->gdi->primary_buffer == nullptr) {
        rdp_update_unlock(context->update);
        return;
    }
    Frame frame;
    frame.width = static_cast<std::uint32_t>(context->gdi->width);
    frame.height = static_cast<std::uint32_t>(context->gdi->height);
    frame.stride = context->gdi->stride;
    frame.sequence = sequence_++;
    frame.captured_at = std::chrono::steady_clock::now();
    const std::size_t size = static_cast<std::size_t>(frame.stride) * frame.height;
    frame.pixels.resize(size);
    std::memcpy(frame.pixels.data(), context->gdi->primary_buffer, size);
    rdp_update_unlock(context->update);
    ++published_frames_;
    on_frame_(std::move(frame));
}

void RdpFrameSource::drain_input(rdpContext* context) {
    std::deque<std::string> pending;
    {
        std::lock_guard lock(input_mutex_);
        pending.swap(input_events_);
    }

    for (const std::string& serialized : pending) {
        try {
            const auto event = nlohmann::json::parse(serialized);
            const std::string type = event.value("type", "");
            if (type == "pointer") {
                const auto x = static_cast<UINT16>(std::clamp(event.value("x", 0.0), 0.0, 1.0) * (options_.width - 1));
                const auto y = static_cast<UINT16>(std::clamp(event.value("y", 0.0), 0.0, 1.0) * (options_.height - 1));
                freerdp_input_send_mouse_event(context->input, PTR_FLAGS_MOVE, x, y);
                ++processed_input_events_;
            }
            else if (type == "pointer-button") {
                const int button = event.value("button", 0);
                UINT16 flags = button == 0 ? PTR_FLAGS_BUTTON1 :
                               button == 1 ? PTR_FLAGS_BUTTON3 : PTR_FLAGS_BUTTON2;
                if (event.value("down", false)) flags |= PTR_FLAGS_DOWN;
                const auto x = static_cast<UINT16>(std::clamp(event.value("x", 0.0), 0.0, 1.0) * (options_.width - 1));
                const auto y = static_cast<UINT16>(std::clamp(event.value("y", 0.0), 0.0, 1.0) * (options_.height - 1));
                freerdp_input_send_mouse_event(context->input, flags, x, y);
                ++processed_input_events_;
            }
            else if (type == "wheel") {
                const int delta = std::clamp(event.value("delta", 0), -255, 255);
                if (delta != 0) {
                    UINT16 flags = PTR_FLAGS_WHEEL;
                    if (delta < 0) flags |= PTR_FLAGS_WHEEL_NEGATIVE;
                    flags |= static_cast<UINT16>(std::abs(delta));
                    freerdp_input_send_mouse_event(context->input, flags, 0, 0);
                    ++processed_input_events_;
                }
            }
            else if (type == "key") {
                const std::uint32_t scancode = scancode_for(event.value("code", ""));
                if (scancode != 0) {
                    freerdp_input_send_keyboard_event_ex(context->input,
                        event.value("down", false), false, scancode);
                    ++processed_input_events_;
                }
                else {
                    const std::string key = event.value("key", "");
                    const auto code_units = utf8_to_utf16(key);
                    if (!key.empty() && key != "Dead" && key != "Process" &&
                        code_units.size() <= 2) {
                        const UINT16 flags = event.value("down", false) ? 0 : KBD_FLAGS_RELEASE;
                        for (const UINT16 code_unit : code_units)
                            freerdp_input_send_unicode_keyboard_event(context->input, flags, code_unit);
                        ++processed_input_events_;
                    }
                }
            }
            else if (type == "text") {
                const std::string text = event.value("text", "");
                if (text.size() > 16 * 1024) throw std::runtime_error("text input is too large");
                for (const UINT16 code_unit : utf8_to_utf16(text)) {
                    if (code_unit == '\n' || code_unit == '\r') {
                        freerdp_input_send_keyboard_event_ex(context->input, TRUE, FALSE, RDP_SCANCODE_RETURN);
                        freerdp_input_send_keyboard_event_ex(context->input, FALSE, FALSE, RDP_SCANCODE_RETURN);
                    } else if (code_unit == '\t') {
                        freerdp_input_send_keyboard_event_ex(context->input, TRUE, FALSE, RDP_SCANCODE_TAB);
                        freerdp_input_send_keyboard_event_ex(context->input, FALSE, FALSE, RDP_SCANCODE_TAB);
                    } else {
                        freerdp_input_send_unicode_keyboard_event(context->input, 0, code_unit);
                        freerdp_input_send_unicode_keyboard_event(context->input, KBD_FLAGS_RELEASE, code_unit);
                    }
                    processed_input_events_ += 2;
                }
            }
        }
        catch (const std::exception& error) {
            std::cerr << "invalid input event: " << error.what() << '\n';
        }
    }
}

void RdpFrameSource::run(std::stop_token stop_token, FrameHandler on_frame) {
    on_frame_ = std::move(on_frame);
    std::size_t consecutive_failures = 0;
    while (!stop_token.stop_requested()) {
    if (status_handler_) status_handler_("connecting");
    RDP_CLIENT_ENTRY_POINTS entry_points {};
    entry_points.Size = sizeof(entry_points);
    entry_points.Version = RDP_CLIENT_INTERFACE_VERSION;
    entry_points.ContextSize = sizeof(GatewayRdpContext);
    entry_points.ClientNew = client_new;
    entry_points.ClientFree = client_free;

    rdpContext* context = freerdp_client_context_new(&entry_points);
    if (context == nullptr) {
        throw std::runtime_error("unable to allocate FreeRDP context");
    }
    freerdp* instance = context->instance;
    gateway_context(context)->owner = this;
    fallback_addin_provider = freerdp_get_current_addin_provider();
    freerdp_register_addin_provider(gateway_addin_provider, 0);

    rdpSettings* settings = instance->context->settings;
    // The service has no home directory. FreeRDP's printer channel needs a
    // writable ConfigPath for its cached driver metadata.
    const char* state_root = std::getenv("REMOTELINK_STATE_DIR");
    const std::string freerdp_state = std::string(
        state_root && *state_root ? state_root : "/var/lib/remotelink") + "/freerdp";
    freerdp_settings_set_string(settings, FreeRDP_ConfigPath, freerdp_state.c_str());
    freerdp_settings_set_string(settings, FreeRDP_ServerHostname, options_.hostname.c_str());
    freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, options_.port);
    freerdp_settings_set_string(settings, FreeRDP_Username, options_.username.c_str());
    freerdp_settings_set_string(settings, FreeRDP_Password, options_.password.c_str());
    if (!options_.domain.empty())
        freerdp_settings_set_string(settings, FreeRDP_Domain, options_.domain.c_str());
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, options_.width);
    freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, options_.height);
    freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32);
    freerdp_settings_set_bool(settings, FreeRDP_SoftwareGdi, TRUE);
    freerdp_set_connection_type(settings, CONNECTION_TYPE_LAN);
    freerdp_settings_set_bool(settings, FreeRDP_NetworkAutoDetect, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxProgressive, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxProgressiveV2, TRUE);
    /* Prefer the progressive RDPGFX codec. AVC decoding is intentionally not
     * advertised here: some headless FreeRDP builds expose AVC capability but
     * do not provide a stable decoder path for a software-GDI client. */
    freerdp_settings_set_bool(settings, FreeRDP_GfxH264, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxAVC444v2, FALSE);
    freerdp_settings_set_bool(settings, FreeRDP_GfxSendQoeAck, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_SurfaceFrameMarkerEnabled, TRUE);
    freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate,
                              options_.ignore_certificate ? TRUE : FALSE);
    if (options_.audio_playback) {
        const char* audio_channel[] = { RDPSND_CHANNEL_NAME, "sys:gateway" };
        if (!freerdp_client_add_static_channel(settings,
                static_cast<int>(std::size(audio_channel)), audio_channel)) {
            freerdp_client_context_free(context);
            throw std::runtime_error("unable to configure RDP audio channel");
        }
    }
    freerdp_settings_set_bool(settings, FreeRDP_RedirectPrinters,
                              options_.redirect_printers ? TRUE : FALSE);
    if (!options_.shared_files_path.empty()) {
        const char* drive_channel[] = { "drive", "Gateway Files", options_.shared_files_path.c_str() };
        if (!freerdp_client_add_device_channel(
                settings, std::size(drive_channel), drive_channel)) {
            std::cerr << "unable to configure Gateway Files drive; continuing without file redirection\n";
        }
    }

    if (!freerdp_connect(instance)) {
        const UINT32 error = freerdp_get_last_error(instance->context);
        freerdp_client_context_free(context);
        const auto delay = reconnect_delay(++consecutive_failures);
        std::cerr << "RDP connection failed with error " << error
                  << "; retrying in " << delay.count() << " seconds\n";
        if (status_handler_) status_handler_("retrying");
        if (!interruptible_wait(stop_token, delay)) break;
        continue;
    }

    if (status_handler_) status_handler_("connected");
    const auto connected_at = std::chrono::steady_clock::now();

    constexpr auto frame_period = std::chrono::microseconds(33'333);
    auto next_frame_at = std::chrono::steady_clock::now();
    while (!stop_token.stop_requested() && !freerdp_shall_disconnect_context(instance->context)) {
        HANDLE handles[64];
        const DWORD count = freerdp_get_event_handles(instance->context, handles, 64);
        if (count == 0) break;
        const auto before_wait = std::chrono::steady_clock::now();
        const auto remaining = next_frame_at > before_wait
            ? std::chrono::duration_cast<std::chrono::milliseconds>(next_frame_at - before_wait).count()
            : 0;
        const DWORD timeout = static_cast<DWORD>(std::clamp<std::int64_t>(remaining, 0, 10));
        const DWORD wait = WaitForMultipleObjects(count, handles, FALSE, timeout);
        if (wait == WAIT_FAILED || !freerdp_check_event_handles(instance->context)) break;
        drain_input(instance->context);
        const auto now = std::chrono::steady_clock::now();
        if (now >= next_frame_at) {
            publish_frame(instance->context);
            next_frame_at += frame_period;
            if (now - next_frame_at > frame_period) next_frame_at = now + frame_period;
        }
    }

    freerdp_disconnect(instance);
    freerdp_client_context_free(context);
    if (!stop_token.stop_requested()) {
        if (std::chrono::steady_clock::now() - connected_at >= std::chrono::seconds(30)) {
            consecutive_failures = 0;
        }
        const auto delay = reconnect_delay(++consecutive_failures);
        std::cerr << "RDP session disconnected; retrying in "
                  << delay.count() << " seconds\n";
        if (status_handler_) status_handler_("retrying");
        if (!interruptible_wait(stop_token, delay)) break;
    }
    }
    if (status_handler_) status_handler_("stopped");
    on_frame_ = {};
}

}  // namespace remotelink
