#include <termin/gui_native/dynamic_texture_lease.hpp>

#include "application_host_internal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.h>

#include <termin/gui_native/canvas.hpp>
#include <termin/gui_native/document.hpp>
#include <termin/gui_native/document_renderer.hpp>

#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>

namespace termin::gui_native {

namespace {

struct CanvasBinding {
    tc_widget_handle handle = tc_widget_handle_invalid();
    CanvasTextureLayer layer = CanvasTextureLayer::Image;
};

[[noreturn]] void lease_error(const std::string& message) {
    tc_log_error("[gui-native-texture] %s", message.c_str());
    throw std::logic_error(message);
}

[[noreturn]] void lease_argument_error(const std::string& message) {
    tc_log_error("[gui-native-texture] %s", message.c_str());
    throw std::invalid_argument(message);
}

bool same_handle(tc_widget_handle lhs, tc_widget_handle rhs) {
    return tc_widget_handle_eq(lhs, rhs);
}

size_t rgba8_byte_count(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        lease_argument_error("RGBA8 texture dimensions must be positive");
    }
    constexpr size_t channels = 4;
    if (static_cast<size_t>(width) >
        std::numeric_limits<size_t>::max() / static_cast<size_t>(height) / channels) {
        lease_argument_error("RGBA8 texture dimensions overflow the address space");
    }
    return static_cast<size_t>(width) * static_cast<size_t>(height) * channels;
}

void require_byte_count(std::span<const uint8_t> pixels, uint32_t width, uint32_t height) {
    const size_t expected = rgba8_byte_count(width, height);
    if (pixels.size() != expected) {
        lease_argument_error("RGBA8 pixel buffer has " + std::to_string(pixels.size()) +
                             " bytes; expected " + std::to_string(expected));
    }
}

} // namespace

class DynamicTextureRecord {
  public:
    std::weak_ptr<GuiApplicationHostLeaseState> state;
    tgfx::TextureHandle texture{};
    DynamicTextureOwnership ownership = DynamicTextureOwnership::Empty;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<CanvasBinding> bindings;
};

namespace {

std::shared_ptr<GuiApplicationHostLeaseState>
require_active(const std::shared_ptr<DynamicTextureRecord>& record, const char* operation) {
    if (!record || record->ownership == DynamicTextureOwnership::Released) {
        lease_error(std::string("DynamicTextureLease::") + operation + " called after release");
    }
    auto state = record->state.lock();
    if (!state) {
        lease_error(std::string("DynamicTextureLease::") + operation +
                    " called after host destruction");
    }
    {
        const std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->open || !state->request_repaint ||
            !state->graphics || !state->document ||
            !state->document->valid()) {
            lease_error(std::string("DynamicTextureLease::") + operation +
                        " called after host or document shutdown");
        }
    }
    if (state->graphics->is_closed()) {
        lease_error(std::string("DynamicTextureLease::") + operation +
                    " called after GraphicsHost shutdown");
    }
    return state;
}

Canvas* resolve_canvas(const GuiApplicationHostLeaseState& state, const CanvasBinding& binding,
                       const char* operation, bool log_failure) {
    tc_widget* widget = tc_ui_document_resolve_widget(state.document->get(), binding.handle);
    auto* canvas = widget ? dynamic_cast<Canvas*>(static_cast<Widget*>(widget->body)) : nullptr;
    if (!canvas && log_failure) {
        tc_log_error("[gui-native-texture] DynamicTextureLease::%s encountered a "
                     "stale or non-Canvas binding",
                     operation);
    }
    return canvas;
}

void validate_bindings(const GuiApplicationHostLeaseState& state,
                       const DynamicTextureRecord& record, const char* operation) {
    for (const CanvasBinding& binding : record.bindings) {
        if (!resolve_canvas(state, binding, operation, true)) {
            lease_error(std::string("DynamicTextureLease::") + operation +
                        " encountered a stale Canvas binding");
        }
    }
}

void apply_binding(Canvas& canvas, CanvasTextureLayer layer, tgfx::TextureHandle texture,
                   uint32_t width, uint32_t height) {
    if (layer == CanvasTextureLayer::Image) {
        if (texture) {
            canvas.set_texture(texture.id,
                               tc_ui_size{static_cast<float>(width), static_cast<float>(height)});
        } else {
            canvas.clear_texture();
        }
    } else {
        canvas.set_overlay_texture(texture.id);
    }
}

void apply_bindings(const GuiApplicationHostLeaseState& state, const DynamicTextureRecord& record) {
    for (const CanvasBinding& binding : record.bindings) {
        Canvas* canvas = resolve_canvas(state, binding, "apply", false);
        // All callers validate first or are host-shutdown cleanup paths.
        if (canvas) {
            apply_binding(*canvas, binding.layer, record.texture, record.width, record.height);
        }
    }
}

bool clear_canvas_bindings(const GuiApplicationHostLeaseState& state, DynamicTextureRecord& record,
                           const char* operation) {
    bool stale = false;
    for (const CanvasBinding& binding : record.bindings) {
        Canvas* canvas = resolve_canvas(state, binding, operation, true);
        if (canvas) {
            apply_binding(*canvas, binding.layer, {}, 0, 0);
        } else {
            stale = true;
        }
    }
    return stale;
}

void destroy_owned_texture(GuiApplicationHostLeaseState& state, DynamicTextureRecord& record) {
    if (record.ownership == DynamicTextureOwnership::Owned && record.texture) {
        state.graphics->device().destroy(record.texture);
    }
    record.texture = {};
    record.width = 0;
    record.height = 0;
}

bool reset_record(GuiApplicationHostLeaseState& state, DynamicTextureRecord& record,
                  const char* operation) {
    const bool stale = clear_canvas_bindings(state, record, operation);
    destroy_owned_texture(state, record);
    record.ownership = DynamicTextureOwnership::Empty;
    state.request_repaint();
    return stale;
}

void release_record(const std::shared_ptr<DynamicTextureRecord>& record) noexcept {
    if (!record || record->ownership == DynamicTextureOwnership::Released)
        return;
    auto state = record->state.lock();
    if (!state) {
        record->ownership = DynamicTextureOwnership::Released;
        return;
    }
    try {
        bool open = false;
        {
            const std::lock_guard<std::mutex> lock(state->mutex);
            open = state->open && state->request_repaint &&
                   state->graphics && state->document &&
                   state->document->valid();
        }
        if (open && !state->graphics->is_closed()) {
            reset_record(*state, *record, "release");
        } else {
            record->texture = {};
            record->width = 0;
            record->height = 0;
        }
    } catch (const std::exception& error) {
        tc_log_error("[gui-native-texture] lease release cleanup failed: %s", error.what());
    } catch (...) {
        tc_log_error("[gui-native-texture] lease release cleanup failed with "
                     "unknown exception");
    }
    record->ownership = DynamicTextureOwnership::Released;
    record->bindings.clear();
    state->unregister_record(record.get());
}

void dispose_record(const std::shared_ptr<DynamicTextureRecord>& record) noexcept {
    if (!record || record->ownership == DynamicTextureOwnership::Released)
        return;
    release_record(record);
}

} // namespace

void GuiApplicationHostLeaseState::register_record(
    const std::shared_ptr<DynamicTextureRecord>& record) {
    const std::lock_guard<std::mutex> lock(mutex);
    if (!open) {
        lease_error("cannot register a DynamicTextureLease on a closed host");
    }
    records.push_back(record);
}

void GuiApplicationHostLeaseState::unregister_record(const DynamicTextureRecord* record) {
    const std::lock_guard<std::mutex> lock(mutex);
    records.erase(
        std::remove_if(records.begin(), records.end(),
                       [record](const auto& candidate) { return candidate.get() == record; }),
        records.end());
}

void GuiApplicationHostLeaseState::close_all() noexcept {
    std::vector<std::shared_ptr<DynamicTextureRecord>> pending;
    {
        const std::lock_guard<std::mutex> lock(mutex);
        if (!open)
            return;
        open = false;
        pending.swap(records);
    }
    for (const auto& record : pending) {
        if (!record || record->ownership == DynamicTextureOwnership::Released)
            continue;
        try {
            clear_canvas_bindings(*this, *record, "host_close");
            destroy_owned_texture(*this, *record);
        } catch (const std::exception& error) {
            tc_log_error("[gui-native-texture] host shutdown cleanup failed: %s", error.what());
        } catch (...) {
            tc_log_error("[gui-native-texture] host shutdown cleanup failed with "
                         "unknown exception");
        }
        record->ownership = DynamicTextureOwnership::Released;
        record->bindings.clear();
    }
    const std::lock_guard<std::mutex> lock(mutex);
    request_repaint = {};
    graphics = nullptr;
    document = nullptr;
}

struct DynamicTextureLease::Impl {
    std::shared_ptr<DynamicTextureRecord> record;
};

DynamicTextureLease::DynamicTextureLease(
    std::shared_ptr<GuiApplicationHostLeaseState> state)
    : impl_(std::make_unique<Impl>()) {
    impl_->record = std::make_shared<DynamicTextureRecord>();
    impl_->record->state = state;
    state->register_record(impl_->record);
}

DynamicTextureLease::DynamicTextureLease(DocumentRenderer& renderer)
    : DynamicTextureLease(renderer.texture_lease_state()) {}

DynamicTextureLease::~DynamicTextureLease() {
    if (impl_)
        dispose_record(impl_->record);
}

DynamicTextureLease::DynamicTextureLease(DynamicTextureLease&& other) noexcept
    : impl_(std::move(other.impl_)) {}

DynamicTextureLease& DynamicTextureLease::operator=(DynamicTextureLease&& other) noexcept {
    if (this == &other)
        return *this;
    if (impl_ && impl_->record && impl_->record->ownership != DynamicTextureOwnership::Released) {
        dispose_record(impl_->record);
    }
    impl_ = std::move(other.impl_);
    return *this;
}

void DynamicTextureLease::set_rgba8(uint32_t width, uint32_t height,
                                    std::span<const uint8_t> pixels) {
    auto state = require_active(impl_ ? impl_->record : nullptr, "set_rgba8");
    auto& record = *impl_->record;
    require_byte_count(pixels, width, height);
    if (record.ownership == DynamicTextureOwnership::Borrowed) {
        lease_error("DynamicTextureLease::set_rgba8 requires clear() before replacing "
                    "a borrowed texture");
    }
    validate_bindings(*state, record, "set_rgba8");
    auto& device = state->graphics->device();
    if (record.ownership == DynamicTextureOwnership::Owned && record.width == width &&
        record.height == height) {
        device.upload_texture(record.texture, pixels);
        state->request_repaint();
        return;
    }

    tgfx::TextureDesc description;
    description.width = width;
    description.height = height;
    description.format = tgfx::PixelFormat::RGBA8_UNorm;
    description.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
    const tgfx::TextureHandle replacement = device.create_texture(description);
    if (!replacement) {
        lease_error("DynamicTextureLease::set_rgba8 failed to create a texture");
    }
    try {
        device.upload_texture(replacement, pixels);
    } catch (...) {
        device.destroy(replacement);
        throw;
    }
    destroy_owned_texture(*state, record);
    record.texture = replacement;
    record.width = width;
    record.height = height;
    record.ownership = DynamicTextureOwnership::Owned;
    apply_bindings(*state, record);
    state->request_repaint();
}

void DynamicTextureLease::update_region_rgba8(uint32_t x, uint32_t y, uint32_t width,
                                              uint32_t height, std::span<const uint8_t> pixels) {
    auto state = require_active(impl_ ? impl_->record : nullptr, "update_region_rgba8");
    auto& record = *impl_->record;
    if (record.ownership != DynamicTextureOwnership::Owned) {
        lease_error("DynamicTextureLease::update_region_rgba8 requires an owned texture");
    }
    require_byte_count(pixels, width, height);
    if (x > record.width || y > record.height || width > record.width - x ||
        height > record.height - y) {
        lease_argument_error("DynamicTextureLease::update_region_rgba8 region is out of bounds");
    }
    validate_bindings(*state, record, "update_region_rgba8");
    state->graphics->device().upload_texture_region(record.texture, x, y, width, height, pixels);
    state->request_repaint();
}

void DynamicTextureLease::borrow(tgfx::GraphicsHost& texture_owner, tgfx::TextureHandle texture) {
    auto state = require_active(impl_ ? impl_->record : nullptr, "borrow");
    auto& record = *impl_->record;
    if (record.ownership != DynamicTextureOwnership::Empty) {
        lease_error("DynamicTextureLease::borrow requires an empty lease; call "
                    "clear() first");
    }
    if (&texture_owner != state->graphics) {
        lease_argument_error("DynamicTextureLease::borrow rejected a texture from "
                             "another GraphicsHost");
    }
    if (!texture) {
        lease_argument_error("DynamicTextureLease::borrow requires a valid texture handle");
    }
    const tgfx::TextureDesc description = state->graphics->device().texture_desc(texture);
    if (!tgfx::has_flag(description.usage, tgfx::TextureUsage::Sampled)) {
        lease_argument_error("DynamicTextureLease::borrow requires a live sampled texture");
    }
    validate_bindings(*state, record, "borrow");
    record.texture = texture;
    record.width = description.width;
    record.height = description.height;
    record.ownership = DynamicTextureOwnership::Borrowed;
    apply_bindings(*state, record);
    state->request_repaint();
}

void DynamicTextureLease::bind_canvas(Canvas& canvas, CanvasTextureLayer layer) {
    auto state = require_active(impl_ ? impl_->record : nullptr, "bind_canvas");
    if (!tc_ui_document_handle_eq(canvas.document(), state->document->get())) {
        lease_argument_error("DynamicTextureLease::bind_canvas rejected a Canvas "
                             "from another Document");
    }
    tc_widget* resolved = tc_ui_document_resolve_widget(state->document->get(), canvas.handle());
    if (!resolved || resolved != canvas.c_widget()) {
        lease_error("DynamicTextureLease::bind_canvas requires a live Canvas");
    }
    const auto duplicate = std::find_if(
        impl_->record->bindings.begin(), impl_->record->bindings.end(),
        [&canvas, layer](const CanvasBinding& binding) {
            return binding.layer == layer && same_handle(binding.handle, canvas.handle());
        });
    if (duplicate == impl_->record->bindings.end()) {
        impl_->record->bindings.push_back(CanvasBinding{canvas.handle(), layer});
    }
    apply_binding(canvas, layer, impl_->record->texture, impl_->record->width,
                  impl_->record->height);
    state->request_repaint();
}

void DynamicTextureLease::unbind_canvas(Canvas& canvas, CanvasTextureLayer layer) {
    auto state = require_active(impl_ ? impl_->record : nullptr, "unbind_canvas");
    if (!tc_ui_document_handle_eq(canvas.document(), state->document->get())) {
        lease_argument_error("DynamicTextureLease::unbind_canvas rejected a Canvas "
                             "from another Document");
    }
    const auto iterator = std::find_if(
        impl_->record->bindings.begin(), impl_->record->bindings.end(),
        [&canvas, layer](const CanvasBinding& binding) {
            return binding.layer == layer && same_handle(binding.handle, canvas.handle());
        });
    if (iterator == impl_->record->bindings.end())
        return;
    tc_widget* resolved = tc_ui_document_resolve_widget(state->document->get(), canvas.handle());
    if (!resolved || resolved != canvas.c_widget()) {
        lease_error("DynamicTextureLease::unbind_canvas encountered a stale Canvas");
    }
    apply_binding(canvas, layer, {}, 0, 0);
    impl_->record->bindings.erase(iterator);
    state->request_repaint();
}

void DynamicTextureLease::clear() {
    auto state = require_active(impl_ ? impl_->record : nullptr, "clear");
    if (reset_record(*state, *impl_->record, "clear")) {
        lease_error("DynamicTextureLease::clear encountered a stale Canvas binding; "
                    "the texture was still released");
    }
}

void DynamicTextureLease::release() {
    if (!impl_ || !impl_->record || impl_->record->ownership == DynamicTextureOwnership::Released) {
        return;
    }
    require_active(impl_->record, "release");
    release_record(impl_->record);
}

DynamicTextureOwnership DynamicTextureLease::ownership() const {
    return impl_ && impl_->record ? impl_->record->ownership : DynamicTextureOwnership::Released;
}

tgfx::TextureHandle DynamicTextureLease::texture() const {
    return impl_ && impl_->record ? impl_->record->texture : tgfx::TextureHandle{};
}

uint32_t DynamicTextureLease::width() const {
    return impl_ && impl_->record ? impl_->record->width : 0;
}

uint32_t DynamicTextureLease::height() const {
    return impl_ && impl_->record ? impl_->record->height : 0;
}

bool DynamicTextureLease::empty() const { return ownership() == DynamicTextureOwnership::Empty; }

bool DynamicTextureLease::released() const {
    return ownership() == DynamicTextureOwnership::Released;
}

} // namespace termin::gui_native
