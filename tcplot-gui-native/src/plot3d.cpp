#include <tcplot/gui_native/plot3d.hpp>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <tcbase/tc_log.h>
#include <tcplot/gpu_host.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/graphics_host.hpp>
#include <tgfx2/render_context.hpp>

namespace tcplot::gui_native {

    class Plot3D::Surface final : public termin::gui_native::ViewportSurfaceHost {
    public:
        Surface()
            : chart_(tc_retained_chart3d_create(nullptr)) {
            if (!chart_) {
                throw std::runtime_error("tcplot Plot3D failed to create retained chart");
            }
        }

        ~Surface() override {
            release_gpu();
            tc_retained_chart3d_destroy(chart_);
        }

        tc_retained_chart3d* chart() const {
            return chart_;
        }

        bool is_valid() const override {
            return chart_ != nullptr;
        }

        uint32_t texture_id() const override {
            return texture_id_;
        }

        termin::gui_native::ViewportSurfaceSize framebuffer_size() const override {
            return size_;
        }

        bool resize(int width, int height) override {
            if (width <= 0 || height <= 0) {
                tc_log_error("[tcplot-gui-native] Plot3D rejected invalid surface size %dx%d", width, height);
                return false;
            }
            size_ = {width, height};
            return true;
        }

        bool pointer_move(double x, double y) override {
            pointer_x_ = x;
            pointer_y_ = y;
            tc_retained_chart3d_pointer_move(chart_, static_cast<float>(x), static_cast<float>(y));
            return true;
        }

        bool pointer_button(int button, int action, int, uint32_t) override {
            if (action == 1) {
                return tc_retained_chart3d_pointer_down(
                           chart_, static_cast<float>(pointer_x_), static_cast<float>(pointer_y_), button) != 0;
            }
            if (action == 0) {
                tc_retained_chart3d_pointer_up(
                    chart_, static_cast<float>(pointer_x_), static_cast<float>(pointer_y_), button);
                return true;
            }
            return false;
        }

        bool scroll(double, double y, int) override {
            return tc_retained_chart3d_wheel(
                       chart_, static_cast<float>(pointer_x_), static_cast<float>(pointer_y_), static_cast<float>(y)) !=
                   0;
        }

        bool key(int, int, int, int) override {
            return false;
        }

        bool text(uint32_t) override {
            return false;
        }

        void prepare(tgfx::RenderContext2& context,
                     tgfx::FontAtlas& font,
                     int width,
                     int height) {
            tgfx::GraphicsHost* graphics = context.graphics_host();
            if (!graphics) {
                throw std::runtime_error("tcplot Plot3D requires a RenderContext2 owned by GraphicsHost");
            }
            if (graphics_ != graphics || font_ != &font) {
                release_gpu();
                gpu_host_ = std::make_unique<tcplot::GpuHost>(*graphics, font);
                if (!tc_retained_chart3d_attach_gpu_host(chart_, gpu_host_.get())) {
                    gpu_host_.reset();
                    throw std::runtime_error("tcplot Plot3D failed to attach the graphics host");
                }
                graphics_ = graphics;
                font_ = &font;
            }
            if (!resize(width, height)) {
                throw std::runtime_error("tcplot Plot3D failed to resize its render target");
            }
            texture_id_ = tc_retained_chart3d_render(chart_, width, height);
            if (texture_id_ == 0) {
                throw std::runtime_error("tcplot Plot3D retained render produced no texture");
            }
        }

        void release_gpu() {
            if (chart_) {
                tc_retained_chart3d_detach_gpu_host(chart_);
            }
            gpu_host_.reset();
            graphics_ = nullptr;
            font_ = nullptr;
            texture_id_ = 0;
        }

    private:
        tc_retained_chart3d* chart_ = nullptr;
        std::unique_ptr<tcplot::GpuHost> gpu_host_;
        tgfx::GraphicsHost* graphics_ = nullptr;
        tgfx::FontAtlas* font_ = nullptr;
        termin::gui_native::ViewportSurfaceSize size_{};
        uint32_t texture_id_ = 0;
        double pointer_x_ = 0.0;
        double pointer_y_ = 0.0;
    };

    Plot3D::Plot3D()
        : surface_(std::make_shared<Surface>()) {
        set_debug_name("Plot3D");
        set_preferred_size({420.0f, 300.0f});
        set_surface_host(surface_);
    }

    Plot3D::~Plot3D() {
        detach_surface();
        surface_->release_gpu();
    }

    tc_retained_chart3d* Plot3D::chart() const {
        return surface_->chart();
    }

    void Plot3D::require_equal(std::span<const double> x,
                               std::span<const double> y,
                               std::span<const double> z) {
        if (x.size() != y.size() || x.size() != z.size()) {
            throw std::invalid_argument("tcplot Plot3D x, y and z arrays must have equal size");
        }
    }

    void Plot3D::invalidate() {
        mark_dirty(TC_WIDGET_DIRTY_STATE | TC_WIDGET_DIRTY_PAINT);
    }

    tc_plot_item3d_handle Plot3D::add_line(std::span<const double> x,
                                           std::span<const double> y,
                                           std::span<const double> z,
                                           const tc_line_item3d_style& style) {
        require_equal(x, y, z);
        const auto item = tc_retained_chart3d_add_line(chart(), x.data(), y.data(), z.data(), x.size(), &style);
        if (item.scene_id == 0)
            throw std::runtime_error("tcplot Plot3D failed to add line data");
        invalidate();
        return item;
    }

    tc_plot_item3d_handle Plot3D::add_scatter(std::span<const double> x,
                                              std::span<const double> y,
                                              std::span<const double> z,
                                              const tc_scatter_item3d_style& style) {
        require_equal(x, y, z);
        const auto item = tc_retained_chart3d_add_scatter(chart(), x.data(), y.data(), z.data(), x.size(), &style);
        if (item.scene_id == 0)
            throw std::runtime_error("tcplot Plot3D failed to add scatter data");
        invalidate();
        return item;
    }

    tc_plot_item3d_handle Plot3D::add_surface(std::span<const double> x,
                                              std::span<const double> y,
                                              std::span<const double> z,
                                              uint32_t rows,
                                              uint32_t columns,
                                              const tc_surface_item3d_style& style) {
        require_equal(x, y, z);
        if (rows < 2 || columns < 2 || x.size() != static_cast<size_t>(rows) * columns) {
            throw std::invalid_argument("tcplot Plot3D surface shape does not match its data");
        }
        const auto item =
            tc_retained_chart3d_add_surface(chart(), x.data(), y.data(), z.data(), rows, columns, &style);
        if (item.scene_id == 0)
            throw std::runtime_error("tcplot Plot3D failed to add surface data");
        invalidate();
        return item;
    }

    bool Plot3D::destroy_item(tc_plot_item3d_handle item) {
        const bool destroyed = tc_retained_chart3d_destroy_item(chart(), item) != 0;
        if (destroyed)
            invalidate();
        return destroyed;
    }

    void Plot3D::clear() {
        tc_retained_chart3d_clear_data(chart());
        invalidate();
    }

    void Plot3D::set_axis_labels(const char* x, const char* y, const char* z) {
        tc_retained_chart3d_set_axis_labels(chart(), x ? x : "x", y ? y : "y", z ? z : "z");
        invalidate();
    }

    bool Plot3D::set_axis_scale(float x, float y, float z) {
        const bool changed = tc_retained_chart3d_set_axis_scale(chart(), x, y, z) != 0;
        if (changed)
            invalidate();
        return changed;
    }

    bool Plot3D::set_surface_shading(bool enabled, float strength) {
        const bool changed = tc_retained_chart3d_set_surface_shading(chart(), enabled ? 1 : 0, strength) != 0;
        if (changed)
            invalidate();
        return changed;
    }

    bool Plot3D::set_light_direction(float x, float y, float z) {
        const bool changed = tc_retained_chart3d_set_light_direction(chart(), x, y, z) != 0;
        if (changed)
            invalidate();
        return changed;
    }

    void Plot3D::fit_camera() {
        tc_retained_chart3d_fit_camera(chart());
        invalidate();
    }

    void Plot3D::reset_camera() {
        tc_retained_chart3d_reset_camera(chart());
        invalidate();
    }

    bool Plot3D::camera(tc_orbit_camera3d_state& state) const {
        return tc_retained_chart3d_get_camera(chart(), &state) != 0;
    }

    bool Plot3D::set_camera(const tc_orbit_camera3d_state& state) {
        const bool changed = tc_retained_chart3d_set_camera(chart(), &state) != 0;
        if (changed)
            invalidate();
        return changed;
    }

    uint64_t Plot3D::scene_id() const {
        return tc_retained_chart3d_scene_id(chart());
    }

    size_t Plot3D::item_count() const {
        return tc_retained_chart3d_item_count(chart());
    }

    uint32_t Plot3D::texture_id() const {
        return surface_->texture_id();
    }

    void Plot3D::layout(tc_ui_document_handle document, tc_ui_rect rect) {
        // The physical render target is selected in prepare_render after the
        // SceneView portal transform and presentation density are known.
        termin::gui_native::NativeWidget::layout(document, rect);
    }

    void Plot3D::prepare_render(tgfx::RenderContext2& context,
                                tgfx::FontAtlas& default_font,
                                float density_scale) {
        const tc_ui_rect rect = bounds();
        const tc_ui_uniform_transform transform = subtree_transform();
        const float scale = std::max(0.01f, density_scale * transform.scale);
        const int width = std::max(1, static_cast<int>(std::round(rect.width * scale)));
        const int height = std::max(1, static_cast<int>(std::round(rect.height * scale)));
        surface_->prepare(context, default_font, width, height);
        mark_dirty(TC_WIDGET_DIRTY_PAINT);
    }

    void Plot3D::release_render_resources() {
        surface_->release_gpu();
    }

    void Plot3D::on_destroy(tc_ui_document_handle) {
        detach_surface();
        surface_->release_gpu();
    }

} // namespace tcplot::gui_native
