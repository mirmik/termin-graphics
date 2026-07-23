#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <termin/gui_native/application_host.hpp>
#include <termin/gui_native/canvas.hpp>
#include <termin/gui_native/dynamic_texture_lease.hpp>

#include <tgfx2/graphics_host.hpp>
#include <tgfx2/i_render_device.hpp>

namespace {

class RecordingDevice final : public tgfx::IRenderDevice {
public:
    tgfx::BackendType backend_type() const override {
        return tgfx::BackendType::Null;
    }
    tgfx::BackendCapabilities capabilities() const override { return {}; }
    void wait_idle() override {}

    tgfx::BufferHandle create_buffer(const tgfx::BufferDesc&) override {
        return {};
    }
    tgfx::TextureHandle create_texture(const tgfx::TextureDesc& description) override {
        const tgfx::TextureHandle handle{next_texture_id_++};
        textures.emplace(handle.id, description);
        ++create_count;
        return handle;
    }
    tgfx::SamplerHandle create_sampler(const tgfx::SamplerDesc&) override {
        return {};
    }
    tgfx::ShaderHandle create_shader(const tgfx::ShaderDesc&) override {
        return {};
    }
    tgfx::PipelineHandle create_pipeline(const tgfx::PipelineDesc&) override {
        return {};
    }
    tgfx::ResourceSetHandle create_bound_resource_set(
        const tgfx::BoundResourceSetDesc&) override {
        return {};
    }

    void destroy(tgfx::BufferHandle) override {}
    void destroy(tgfx::TextureHandle handle) override {
        destroyed.push_back(handle.id);
        textures.erase(handle.id);
    }
    void destroy(tgfx::SamplerHandle) override {}
    void destroy(tgfx::ShaderHandle) override {}
    void destroy(tgfx::PipelineHandle) override {}
    void destroy(tgfx::ResourceSetHandle) override {}

    void upload_buffer(
        tgfx::BufferHandle,
        std::span<const uint8_t>,
        uint64_t) override {}
    void upload_texture(
        tgfx::TextureHandle handle,
        std::span<const uint8_t> data,
        uint32_t) override {
        if (!textures.contains(handle.id)) {
            throw std::logic_error("upload to unknown texture");
        }
        full_uploads.push_back({handle.id, data.size()});
    }
    void upload_texture_region(
        tgfx::TextureHandle handle,
        uint32_t x,
        uint32_t y,
        uint32_t width,
        uint32_t height,
        std::span<const uint8_t> data,
        uint32_t) override {
        if (!textures.contains(handle.id)) {
            throw std::logic_error("region upload to unknown texture");
        }
        region_uploads.push_back(
            {handle.id, x, y, width, height, data.size()});
    }
    void read_buffer(
        tgfx::BufferHandle,
        std::span<uint8_t>,
        uint64_t) override {}

    tgfx::TextureDesc texture_desc(tgfx::TextureHandle handle) const override {
        const auto iterator = textures.find(handle.id);
        return iterator != textures.end() ? iterator->second : tgfx::TextureDesc{};
    }
    std::unique_ptr<tgfx::ICommandList> create_command_list(
        tgfx::QueueType) override {
        return nullptr;
    }
    void submit(tgfx::ICommandList&) override {}
    void present() override {}

    struct FullUpload {
        uint32_t texture;
        size_t bytes;
    };
    struct RegionUpload {
        uint32_t texture;
        uint32_t x;
        uint32_t y;
        uint32_t width;
        uint32_t height;
        size_t bytes;
    };

    uint32_t create_count = 0;
    std::vector<uint32_t> destroyed;
    std::vector<FullUpload> full_uploads;
    std::vector<RegionUpload> region_uploads;
    std::unordered_map<uint32_t, tgfx::TextureDesc> textures;

private:
    uint32_t next_texture_id_ = 1;
};

class InjectedWindow final : public termin::BackendWindow {
public:
    explicit InjectedWindow(tgfx::GraphicsHost& graphics)
        : graphics_(&graphics) {}

    tgfx::BackendType backend_type() const override {
        return graphics_->device().backend_type();
    }
    tgfx::GraphicsHost& graphics_host() const override { return *graphics_; }
    tgfx::PresentationMode requested_presentation_mode() const override {
        return tgfx::PresentationMode::Immediate;
    }
    tgfx::PresentationMode presentation_mode() const override {
        return tgfx::PresentationMode::Immediate;
    }
    bool should_close() const override { return false; }
    void set_should_close(bool) override {}
    void maximize() override {}
    void set_title(const std::string&) override {}
    void set_size(int, int) override {}
    void set_fullscreen(bool) override {}
    void set_text_input_enabled(bool) override {}
    void set_cursor(termin::WindowCursor) override {}
    std::string clipboard_text() const override { return {}; }
    bool set_clipboard_text(const std::string&) override { return true; }
    void close() override {}
    bool poll_event(termin::WindowEvent&) override { return false; }
    std::pair<int, int> window_size() const override { return {320, 200}; }
    std::pair<int, int> framebuffer_size() const override {
        return {320, 200};
    }
    void present(tgfx::TextureHandle) override {}

private:
    tgfx::GraphicsHost* graphics_;
};

struct Fixture {
    RecordingDevice* device = nullptr;
    std::unique_ptr<tgfx::GraphicsHost> graphics;
    termin::gui_native::Document document;
    std::unique_ptr<termin::gui_native::GuiWindowHost> host;

    Fixture() {
        auto owned_device = std::make_unique<RecordingDevice>();
        device = owned_device.get();
        graphics = tgfx::GraphicsHost::adopt_isolated_device(
            std::move(owned_device));
        termin::gui_native::GuiWindowConfig config;
        config.font_path = TERMIN_GUI_NATIVE_TEST_FONT;
        host = std::make_unique<termin::gui_native::GuiWindowHost>(
            *graphics,
            document,
            config,
            std::make_unique<InjectedWindow>(*graphics));
    }

    ~Fixture() {
        if (host && host->is_open()) host->close();
        document.close();
        if (graphics && !graphics->is_closed()) graphics->close();
    }
};

size_t destroy_count(const RecordingDevice& device, uint32_t texture) {
    return static_cast<size_t>(std::count(
        device.destroyed.begin(), device.destroyed.end(), texture));
}

void test_owned_updates_resize_and_canvas_binding() {
    Fixture fixture;
    auto* canvas = new termin::gui_native::Canvas();
    fixture.document.adopt(canvas);

    termin::gui_native::DynamicTextureLease lease(*fixture.host);
    lease.bind_canvas(*canvas);
    std::vector<uint8_t> four_by_three(4 * 3 * 4, 17);
    lease.set_rgba8(4, 3, four_by_three);
    const auto first = lease.texture();
    assert(first);
    assert(canvas->texture_id() == first.id);
    assert(canvas->image_size().width == 4.0f);
    assert(canvas->image_size().height == 3.0f);
    assert(lease.ownership() ==
           termin::gui_native::DynamicTextureOwnership::Owned);
    assert(fixture.device->create_count == 1);
    assert(fixture.device->full_uploads.size() == 1);

    lease.set_rgba8(4, 3, four_by_three);
    assert(lease.texture() == first);
    assert(fixture.device->create_count == 1);
    assert(fixture.device->full_uploads.size() == 2);

    std::vector<uint8_t> region(2 * 2 * 4, 31);
    lease.update_region_rgba8(1, 1, 2, 2, region);
    assert(fixture.device->region_uploads.size() == 1);
    const auto& upload = fixture.device->region_uploads.front();
    assert(upload.texture == first.id && upload.x == 1 && upload.y == 1);
    assert(upload.width == 2 && upload.height == 2 && upload.bytes == 16);

    std::vector<uint8_t> resized(2 * 5 * 4, 9);
    lease.set_rgba8(2, 5, resized);
    const auto second = lease.texture();
    assert(second && second != first);
    assert(destroy_count(*fixture.device, first.id) == 1);
    assert(canvas->texture_id() == second.id);
    assert(canvas->image_size().width == 2.0f);
    assert(canvas->image_size().height == 5.0f);

    lease.clear();
    assert(lease.empty());
    assert(canvas->texture_id() == 0);
    assert(destroy_count(*fixture.device, second.id) == 1);
    lease.release();
    lease.release();
    assert(lease.released());
    assert(destroy_count(*fixture.device, second.id) == 1);
}

void test_borrowed_ownership_and_domain_validation() {
    Fixture fixture;
    tgfx::TextureDesc description;
    description.width = 7;
    description.height = 6;
    description.usage = tgfx::TextureUsage::Sampled;
    const auto borrowed = fixture.device->create_texture(description);

    termin::gui_native::DynamicTextureLease lease(*fixture.host);
    lease.borrow(*fixture.graphics, borrowed);
    assert(lease.ownership() ==
           termin::gui_native::DynamicTextureOwnership::Borrowed);
    assert(lease.width() == 7 && lease.height() == 6);
    lease.clear();
    assert(destroy_count(*fixture.device, borrowed.id) == 0);

    auto other_device = std::make_unique<RecordingDevice>();
    auto other_graphics = tgfx::GraphicsHost::adopt_isolated_device(
        std::move(other_device));
    bool mismatch_rejected = false;
    try {
        lease.borrow(*other_graphics, borrowed);
    } catch (const std::invalid_argument&) {
        mismatch_rejected = true;
    }
    assert(mismatch_rejected);
    other_graphics->close();
}

void test_stale_canvas_and_host_shutdown() {
    Fixture fixture;
    auto* canvas = new termin::gui_native::Canvas();
    const tc_widget_handle canvas_handle = fixture.document.adopt(canvas);
    termin::gui_native::DynamicTextureLease lease(*fixture.host);
    lease.bind_canvas(*canvas, termin::gui_native::CanvasTextureLayer::Overlay);
    std::vector<uint8_t> pixels(3 * 3 * 4, 1);
    lease.set_rgba8(3, 3, pixels);
    const auto owned = lease.texture();
    assert(canvas->overlay_texture_id() == owned.id);
    assert(tc_ui_document_destroy_widget(fixture.document.get(), canvas_handle));

    bool stale_rejected = false;
    try {
        lease.update_region_rgba8(0, 0, 1, 1, std::span<const uint8_t>(
            pixels.data(), 4));
    } catch (const std::logic_error&) {
        stale_rejected = true;
    }
    assert(stale_rejected);

    bool stale_reported_on_clear = false;
    try {
        lease.clear();
    } catch (const std::logic_error&) {
        stale_reported_on_clear = true;
    }
    assert(stale_reported_on_clear);
    assert(lease.empty());
    assert(destroy_count(*fixture.device, owned.id) == 1);
    lease.release();

    termin::gui_native::DynamicTextureLease shutdown_lease(*fixture.host);
    shutdown_lease.set_rgba8(3, 3, pixels);
    const auto final_owned = shutdown_lease.texture();
    fixture.host->close();
    assert(shutdown_lease.released());
    assert(destroy_count(*fixture.device, final_owned.id) == 1);

    bool shutdown_rejected = false;
    try {
        shutdown_lease.set_rgba8(3, 3, pixels);
    } catch (const std::logic_error&) {
        shutdown_rejected = true;
    }
    assert(shutdown_rejected);
}

void test_cross_thread_update_is_allowed() {
    Fixture fixture;
    termin::gui_native::DynamicTextureLease lease(*fixture.host);
    std::vector<uint8_t> pixels(4, 1);
    std::thread worker([&] {
        lease.set_rgba8(1, 1, pixels);
    });
    worker.join();
    assert(!lease.empty());
}

void test_off_thread_finalization_releases_immediately() {
    Fixture fixture;
    auto lease = std::make_unique<termin::gui_native::DynamicTextureLease>(
        *fixture.host);
    std::vector<uint8_t> pixels(4, 1);
    lease->set_rgba8(1, 1, pixels);
    const auto owned = lease->texture();
    std::thread worker([lease = std::move(lease)]() mutable {
        lease.reset();
    });
    worker.join();
    assert(destroy_count(*fixture.device, owned.id) == 1);
}

} // namespace

int main() {
    try {
        test_owned_updates_resize_and_canvas_binding();
        test_borrowed_ownership_and_domain_validation();
        test_stale_canvas_and_host_shutdown();
        test_cross_thread_update_is_allowed();
        test_off_thread_finalization_releases_immediately();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "dynamic texture lease test failed: %s\n", error.what());
        return 1;
    }
}
