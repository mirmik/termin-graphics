#include "tcplot/retained_chart3d.h"
#include "tcplot/plot_scene3d_render_item_source.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <tgfx2/descriptors.hpp>
#include <tgfx2/enums.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/render_context.hpp>

#include "tcplot/engine3d.hpp"
#include "tcplot/gpu_host.hpp"

namespace {

class RetainedChart3D;

class PlotScene3DRenderItemSource final : public termin::RenderItemSource {
public:
    explicit PlotScene3DRenderItemSource(RetainedChart3D& scene)
        : scene_(&scene) {}

protected:
    const char* source_name() const noexcept override;
    bool collect_items(
        const termin::RenderItemSourceRequest& request,
        termin::RenderItemCollection& output,
        termin::RenderItemSnapshotCounters& counters) override;

private:
    RetainedChart3D* scene_;
};

std::atomic<std::uint64_t> g_next_plot_scene3d_id{1};

tc_plot_item3d_handle invalid_item() {
    return {0, std::numeric_limits<std::uint32_t>::max(), 0};
}

bool finite_color(float r, float g, float b, float a) {
    return std::isfinite(r) && std::isfinite(g) &&
           std::isfinite(b) && std::isfinite(a);
}

tcplot::SurfaceColorMap colormap(std::uint32_t value) {
    if (value > static_cast<std::uint32_t>(tcplot::SurfaceColorMap::Solid)) {
        throw std::invalid_argument("invalid surface colormap");
    }
    return static_cast<tcplot::SurfaceColorMap>(value);
}

tc_surface_item3d_style default_surface_style() {
    return {
        1.0f, 1.0f, 1.0f, 1.0f,
        TC_PLOT_COLORMAP3D_VIRIDIS, 0, 0,
        0, 8, 8, 1.25f,
        0.04f, 0.04f, 0.04f, 1.0f,
    };
}

tc_scatter_item3d_style default_scatter_style() {
    return {1.0f, 0.35f, 0.15f, 1.0f, 4.0f};
}

tc_grid_item3d_style default_grid_style() {
    return {
        0.42f, 0.45f, 0.52f, 1.0f,
        0.95f, 0.24f, 0.22f,
        0.25f, 0.86f, 0.38f,
        0.28f, 0.48f, 1.0f,
        1,
    };
}

void validate(const tc_surface_item3d_style& style) {
    if (!finite_color(
            style.color_r, style.color_g, style.color_b, style.color_a) ||
        !std::isfinite(style.surface_grid_width_px) ||
        style.surface_grid_width_px <= 0 ||
        !finite_color(
            style.surface_grid_r,
            style.surface_grid_g,
            style.surface_grid_b,
            style.surface_grid_a) ||
        style.surface_grid_row_step == 0 ||
        style.surface_grid_col_step == 0) {
        throw std::invalid_argument("invalid retained surface style");
    }
    (void)colormap(style.colormap);
}

void validate(const tc_scatter_item3d_style& style) {
    if (!finite_color(
            style.color_r, style.color_g, style.color_b, style.color_a) ||
        !std::isfinite(style.size) || style.size <= 0) {
        throw std::invalid_argument("invalid retained scatter style");
    }
}

void validate(const tc_grid_item3d_style& style) {
    if (!finite_color(
            style.grid_r, style.grid_g, style.grid_b, style.grid_a) ||
        !finite_color(
            style.x_axis_r, style.x_axis_g, style.x_axis_b, 1.0f) ||
        !finite_color(
            style.y_axis_r, style.y_axis_g, style.y_axis_b, 1.0f) ||
        !finite_color(
            style.z_axis_r, style.z_axis_g, style.z_axis_b, 1.0f)) {
        throw std::invalid_argument("invalid retained grid style");
    }
}

bool same_style(
    const tc_surface_item3d_style& left,
    const tc_surface_item3d_style& right) {
    return left.color_r == right.color_r &&
           left.color_g == right.color_g &&
           left.color_b == right.color_b &&
           left.color_a == right.color_a &&
           left.colormap == right.colormap &&
           left.colormap_reversed == right.colormap_reversed &&
           left.wireframe == right.wireframe &&
           left.surface_grid_visible == right.surface_grid_visible &&
           left.surface_grid_row_step == right.surface_grid_row_step &&
           left.surface_grid_col_step == right.surface_grid_col_step &&
           left.surface_grid_width_px == right.surface_grid_width_px &&
           left.surface_grid_r == right.surface_grid_r &&
           left.surface_grid_g == right.surface_grid_g &&
           left.surface_grid_b == right.surface_grid_b &&
           left.surface_grid_a == right.surface_grid_a;
}

bool same_style(
    const tc_scatter_item3d_style& left,
    const tc_scatter_item3d_style& right) {
    return left.color_r == right.color_r &&
           left.color_g == right.color_g &&
           left.color_b == right.color_b &&
           left.color_a == right.color_a &&
           left.size == right.size;
}

bool same_style(
    const tc_grid_item3d_style& left,
    const tc_grid_item3d_style& right) {
    return left.grid_r == right.grid_r &&
           left.grid_g == right.grid_g &&
           left.grid_b == right.grid_b &&
           left.grid_a == right.grid_a &&
           left.x_axis_r == right.x_axis_r &&
           left.x_axis_g == right.x_axis_g &&
           left.x_axis_b == right.x_axis_b &&
           left.y_axis_r == right.y_axis_r &&
           left.y_axis_g == right.y_axis_g &&
           left.y_axis_b == right.y_axis_b &&
           left.z_axis_r == right.z_axis_r &&
           left.z_axis_g == right.z_axis_g &&
           left.z_axis_b == right.z_axis_b &&
           left.labels_visible == right.labels_visible;
}

std::vector<double> copy_values(const double* values, std::size_t count) {
    if (!values || count == 0) {
        throw std::invalid_argument("retained 3D item data must not be empty");
    }
    std::vector<double> result(values, values + count);
    if (!std::all_of(result.begin(), result.end(), [](double value) {
            return std::isfinite(value);
        })) {
        throw std::invalid_argument("retained 3D item data must be finite");
    }
    return result;
}

class RetainedChart3D {
public:
    explicit RetainedChart3D(tcplot::GpuHost& host)
        : host_(&host),
          scene_id_(g_next_plot_scene3d_id.fetch_add(1)) {
        grid_part_ = add_grid(default_grid_style());
    }

    ~RetainedChart3D() {
        release_gpu();
    }

    std::uint64_t scene_id() const { return scene_id_; }

    std::size_t item_count() const {
        return static_cast<std::size_t>(std::count_if(
            slots_.begin(), slots_.end(),
            [](const Slot& slot) { return slot.alive; }));
    }

    bool valid(tc_plot_item3d_handle handle) const {
        return resolve(handle) != nullptr;
    }

    bool snapshot(
        tc_plot_item3d_handle handle,
        tc_plot_item3d_snapshot& result) const {
        const Slot* slot = resolve(handle);
        if (!slot) return false;
        result.kind = static_cast<std::uint32_t>(slot->kind);
        result.geometry_revision = slot->geometry_revision;
        result.style_revision = slot->style_revision;
        result.gpu_revision = slot->gpu_revision;
        return true;
    }

    tc_plot_item3d_handle add_surface(
        const double* x,
        const double* y,
        const double* z,
        std::uint32_t rows,
        std::uint32_t columns,
        const tc_surface_item3d_style& style) {
        if (rows < 2 || columns < 2) {
            throw std::invalid_argument(
                "retained surface requires at least a 2x2 grid");
        }
        const std::size_t count =
            static_cast<std::size_t>(rows) * columns;
        validate(style);
        std::vector<double> copied_x = copy_values(x, count);
        std::vector<double> copied_y = copy_values(y, count);
        std::vector<double> copied_z = copy_values(z, count);
        Slot& slot = allocate(TC_PLOT_ITEM3D_SURFACE);
        slot.x = std::move(copied_x);
        slot.y = std::move(copied_y);
        slot.z = std::move(copied_z);
        slot.rows = rows;
        slot.columns = columns;
        slot.surface_style = style;
        rebuild(slot);
        bounds_dirty_ = true;
        fit_camera();
        return handle(slot);
    }

    tc_plot_item3d_handle add_scatter(
        const double* x,
        const double* y,
        const double* z,
        std::size_t count,
        const tc_scatter_item3d_style& style) {
        validate(style);
        std::vector<double> copied_x = copy_values(x, count);
        std::vector<double> copied_y = copy_values(y, count);
        std::vector<double> copied_z = copy_values(z, count);
        Slot& slot = allocate(TC_PLOT_ITEM3D_SCATTER);
        slot.x = std::move(copied_x);
        slot.y = std::move(copied_y);
        slot.z = std::move(copied_z);
        slot.scatter_style = style;
        rebuild(slot);
        bounds_dirty_ = true;
        fit_camera();
        return handle(slot);
    }

    bool set_surface_data(
        tc_plot_item3d_handle handle,
        const double* x,
        const double* y,
        const double* z,
        std::uint32_t rows,
        std::uint32_t columns) {
        Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SURFACE);
        if (!slot) return false;
        if (rows < 2 || columns < 2) {
            throw std::invalid_argument(
                "retained surface requires at least a 2x2 grid");
        }
        const std::size_t count =
            static_cast<std::size_t>(rows) * columns;
        std::vector<double> copied_x = copy_values(x, count);
        std::vector<double> copied_y = copy_values(y, count);
        std::vector<double> copied_z = copy_values(z, count);

        // PlotEngine3D remains a temporary renderer body with its own CPU
        // cache. Prepare that copy before committing authoritative state so
        // allocation failures cannot leave the retained item half-mutated.
        if (!slot->engine || !slot->engine->set_surface_data(
                0,
                copied_x,
                copied_y,
                copied_z,
                rows,
                columns)) {
            throw std::runtime_error(
                "failed to update retained surface renderer data");
        }
        slot->x = std::move(copied_x);
        slot->y = std::move(copied_y);
        slot->z = std::move(copied_z);
        slot->rows = rows;
        slot->columns = columns;
        ++slot->geometry_revision;
        ++slot->render_revision;
        slot->gpu_revision = 0;
        bounds_dirty_ = true;
        return true;
    }

    bool set_scatter_data(
        tc_plot_item3d_handle handle,
        const double* x,
        const double* y,
        const double* z,
        std::size_t count) {
        Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SCATTER);
        if (!slot) return false;
        std::vector<double> copied_x = copy_values(x, count);
        std::vector<double> copied_y = copy_values(y, count);
        std::vector<double> copied_z = copy_values(z, count);
        if (!slot->engine || !slot->engine->set_scatter_data(
                0,
                copied_x,
                copied_y,
                copied_z)) {
            throw std::runtime_error(
                "failed to update retained scatter renderer data");
        }
        slot->x = std::move(copied_x);
        slot->y = std::move(copied_y);
        slot->z = std::move(copied_z);
        ++slot->geometry_revision;
        ++slot->render_revision;
        slot->gpu_revision = 0;
        bounds_dirty_ = true;
        return true;
    }

    tc_plot_item3d_handle add_grid(const tc_grid_item3d_style& style) {
        validate(style);
        Slot& slot = allocate(TC_PLOT_ITEM3D_GRID);
        slot.grid_style = style;
        rebuild(slot);
        return handle(slot);
    }

    bool set_surface_style(
        tc_plot_item3d_handle handle,
        const tc_surface_item3d_style& style) {
        Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SURFACE);
        if (!slot) return false;
        validate(style);
        if (same_style(slot->surface_style, style)) return true;
        slot->surface_style = style;
        ++slot->style_revision;
        ++slot->render_revision;
        apply_style(*slot);
        return true;
    }

    bool set_scatter_style(
        tc_plot_item3d_handle handle,
        const tc_scatter_item3d_style& style) {
        Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SCATTER);
        if (!slot) return false;
        validate(style);
        if (same_style(slot->scatter_style, style)) return true;
        slot->scatter_style = style;
        ++slot->style_revision;
        ++slot->render_revision;
        apply_style(*slot);
        return true;
    }

    bool set_grid_style(
        tc_plot_item3d_handle handle,
        const tc_grid_item3d_style& style) {
        Slot* slot = resolve(handle, TC_PLOT_ITEM3D_GRID);
        if (!slot) return false;
        validate(style);
        if (same_style(slot->grid_style, style)) return true;
        slot->grid_style = style;
        ++slot->style_revision;
        ++slot->render_revision;
        apply_style(*slot);
        return true;
    }

    bool surface_style(
        tc_plot_item3d_handle handle,
        tc_surface_item3d_style& style) const {
        const Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SURFACE);
        if (!slot) return false;
        style = slot->surface_style;
        return true;
    }

    bool scatter_style(
        tc_plot_item3d_handle handle,
        tc_scatter_item3d_style& style) const {
        const Slot* slot = resolve(handle, TC_PLOT_ITEM3D_SCATTER);
        if (!slot) return false;
        style = slot->scatter_style;
        return true;
    }

    bool grid_style(
        tc_plot_item3d_handle handle,
        tc_grid_item3d_style& style) const {
        const Slot* slot = resolve(handle, TC_PLOT_ITEM3D_GRID);
        if (!slot) return false;
        style = slot->grid_style;
        return true;
    }

    bool destroy(tc_plot_item3d_handle item) {
        Slot* slot = resolve(item);
        if (!slot) return false;
        const bool affected_bounds =
            slot->kind == TC_PLOT_ITEM3D_SURFACE ||
            slot->kind == TC_PLOT_ITEM3D_SCATTER;
        if (same(grid_part_, item)) grid_part_ = invalid_item();
        slot->engine.reset();
        slot->x.clear();
        slot->y.clear();
        slot->z.clear();
        slot->alive = false;
        ++slot->generation;
        if (slot->generation == 0) ++slot->generation;
        free_.push_back(item.index);
        if (affected_bounds) {
            bounds_dirty_ = true;
            fit_camera();
        }
        return true;
    }

    tc_plot_item3d_handle grid_part() const { return grid_part_; }

    bool set_grid_part(tc_plot_item3d_handle grid) {
        if (grid.scene_id == 0) {
            grid_part_ = invalid_item();
            return true;
        }
        if (!resolve(grid, TC_PLOT_ITEM3D_GRID)) return false;
        grid_part_ = grid;
        return true;
    }

    void set_axis_labels(
        std::string x,
        std::string y,
        std::string z) {
        x_label_ = std::move(x);
        y_label_ = std::move(y);
        z_label_ = std::move(z);
    }

    void set_shading(bool enabled, float strength) {
        if (!std::isfinite(strength)) {
            throw std::invalid_argument("shading strength must be finite");
        }
        shading_ = enabled;
        shading_strength_ = std::clamp(strength, 0.0f, 1.0f);
    }

    void set_light(float x, float y, float z) {
        const float length = std::sqrt(x * x + y * y + z * z);
        if (!std::isfinite(length) || length <= 1e-6f) {
            throw std::invalid_argument("light direction must be non-zero");
        }
        light_ = {x / length, y / length, z / length};
    }

    void set_axis_scale(float x, float y, float z) {
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
            x <= 0 || y <= 0 || z <= 0) {
            throw std::invalid_argument("axis scales must be finite and positive");
        }
        axis_scale_[0] = x;
        axis_scale_[1] = y;
        axis_scale_[2] = z;
    }

    void set_msaa_samples(int samples) {
        if (samples < 1 || samples > 16 ||
            (samples & (samples - 1)) != 0) {
            throw std::invalid_argument(
                "MSAA samples must be a power of two between 1 and 16");
        }
        if (samples == msaa_samples_) return;
        msaa_samples_ = samples;
        release_targets();
    }

    int msaa_samples() const { return msaa_samples_; }

    tc_orbit_camera3d_state camera_state() const {
        return {
            camera_.target.x,
            camera_.target.y,
            camera_.target.z,
            camera_.distance,
            camera_.azimuth,
            camera_.elevation,
            camera_.fov_y,
            camera_.near_clip,
            camera_.far_clip,
        };
    }

    void set_camera(const tc_orbit_camera3d_state& state) {
        if (!std::isfinite(state.target_x) ||
            !std::isfinite(state.target_y) ||
            !std::isfinite(state.target_z) ||
            !std::isfinite(state.distance) || state.distance <= 0 ||
            !std::isfinite(state.azimuth) ||
            !std::isfinite(state.elevation) ||
            !std::isfinite(state.fov_y) || state.fov_y <= 0 ||
            !std::isfinite(state.near_clip) || state.near_clip <= 0 ||
            !std::isfinite(state.far_clip) || state.far_clip <= state.near_clip) {
            throw std::invalid_argument("invalid orbit camera state");
        }
        camera_.target = {state.target_x, state.target_y, state.target_z};
        camera_.distance = state.distance;
        camera_.azimuth = state.azimuth;
        camera_.elevation = state.elevation;
        camera_.fov_y = state.fov_y;
        camera_.near_clip = state.near_clip;
        camera_.far_clip = state.far_clip;
    }

    void fit_camera() {
        double lo[3], hi[3];
        bounds(lo, hi);
        const termin::Vec3f scaled_lo{
            static_cast<float>(lo[0] * axis_scale_[0]),
            static_cast<float>(lo[1] * axis_scale_[1]),
            static_cast<float>(lo[2] * axis_scale_[2])};
        const termin::Vec3f scaled_hi{
            static_cast<float>(hi[0] * axis_scale_[0]),
            static_cast<float>(hi[1] * axis_scale_[1]),
            static_cast<float>(hi[2] * axis_scale_[2])};
        camera_.fit_bounds(
            termin::Vec3f{
                std::min(scaled_lo.x, scaled_hi.x),
                std::min(scaled_lo.y, scaled_hi.y),
                std::min(scaled_lo.z, scaled_hi.z)},
            termin::Vec3f{
                std::max(scaled_lo.x, scaled_hi.x),
                std::max(scaled_lo.y, scaled_hi.y),
                std::max(scaled_lo.z, scaled_hi.z)});
    }

    void reset_camera() {
        // fit_bounds deliberately preserves the viewing direction so that
        // adding or removing data does not surprise an interacting user.
        // An explicit reset has different semantics: restore the canonical
        // OrbitCamera orientation and projection, then frame current data.
        camera_ = tcplot::OrbitCamera{};
        dragging_ = false;
        fit_camera();
    }

    bool pointer_down(float x, float y, int button) {
        if (button != 0 && button != 2) return false;
        dragging_ = true;
        drag_button_ = button;
        drag_x_ = x;
        drag_y_ = y;
        return true;
    }

    void pointer_move(float x, float y) {
        if (!dragging_) return;
        const float dx = x - drag_x_;
        const float dy = y - drag_y_;
        drag_x_ = x;
        drag_y_ = y;
        if (drag_button_ == 0) {
            camera_.orbit(-dx * 0.005f, dy * 0.005f);
        } else {
            camera_.pan(-dx, dy);
        }
    }

    void pointer_up() { dragging_ = false; }

    bool wheel(float delta) {
        if (delta == 0 || !std::isfinite(delta)) return false;
        camera_.zoom(delta > 0 ? 0.9f : 1.0f / 0.9f);
        return true;
    }

    std::uint32_t render(int width, int height) {
        if (width <= 0 || height <= 0) {
            throw std::invalid_argument("invalid retained Chart3D target size");
        }
        ensure_offscreen(width, height);
        synchronize_grids();

        tgfx::RenderContext2& context = host_->ctx();
        const float clear[4] = {0.08f, 0.09f, 0.11f, 1.0f};
        context.begin_frame();
        context.begin_pass(
            color_, depth_, clear, 1.0f, true);

        for (Slot& slot : slots_) {
            if (!slot.alive || slot.kind != TC_PLOT_ITEM3D_SURFACE) continue;
            render_slot(slot, width, height, nullptr);
        }
        if (Slot* grid = resolve(grid_part_, TC_PLOT_ITEM3D_GRID)) {
            render_slot(*grid, width, height, &host_->font());
        }
        for (Slot& slot : slots_) {
            if (!slot.alive || slot.kind != TC_PLOT_ITEM3D_SCATTER) continue;
            render_slot(slot, width, height, nullptr);
        }

        context.end_pass();
        context.end_frame();
        return color_.id;
    }

    void release_gpu() {
        for (Slot& slot : slots_) {
            if (slot.engine) {
                slot.engine->release_gpu_resources();
                slot.gpu_revision = 0;
            }
        }
        release_targets();
    }

private:
    friend class PlotScene3DRenderItemSource;

    struct Slot {
        std::uint32_t index = 0;
        std::uint32_t generation = 1;
        bool alive = false;
        tc_plot_item3d_kind kind = TC_PLOT_ITEM3D_INVALID;
        std::uint64_t geometry_revision = 1;
        std::uint64_t style_revision = 1;
        std::uint64_t render_revision = 1;
        std::uint64_t gpu_revision = 0;
        std::vector<double> x;
        std::vector<double> y;
        std::vector<double> z;
        std::uint32_t rows = 0;
        std::uint32_t columns = 0;
        tc_surface_item3d_style surface_style{};
        tc_scatter_item3d_style scatter_style{};
        tc_grid_item3d_style grid_style{};
        std::unique_ptr<tcplot::PlotEngine3D> engine;
    };

    Slot& allocate(tc_plot_item3d_kind kind) {
        std::uint32_t index;
        if (free_.empty()) {
            index = static_cast<std::uint32_t>(slots_.size());
            slots_.push_back({});
            slots_.back().index = index;
        } else {
            index = free_.back();
            free_.pop_back();
        }
        Slot& slot = slots_[index];
        slot.alive = true;
        slot.kind = kind;
        slot.geometry_revision = 1;
        slot.style_revision = 1;
        slot.render_revision = 1;
        slot.gpu_revision = 0;
        return slot;
    }

    tc_plot_item3d_handle handle(const Slot& slot) const {
        return {scene_id_, slot.index, slot.generation};
    }

    Slot* resolve(tc_plot_item3d_handle item) {
        if (item.scene_id != scene_id_ || item.index >= slots_.size()) {
            return nullptr;
        }
        Slot& slot = slots_[item.index];
        return slot.alive && slot.generation == item.generation
            ? &slot
            : nullptr;
    }

    const Slot* resolve(tc_plot_item3d_handle item) const {
        return const_cast<RetainedChart3D*>(this)->resolve(item);
    }

    Slot* resolve(
        tc_plot_item3d_handle item,
        tc_plot_item3d_kind kind) {
        Slot* slot = resolve(item);
        return slot && slot->kind == kind ? slot : nullptr;
    }

    const Slot* resolve(
        tc_plot_item3d_handle item,
        tc_plot_item3d_kind kind) const {
        const Slot* slot = resolve(item);
        return slot && slot->kind == kind ? slot : nullptr;
    }

    static bool same(
        tc_plot_item3d_handle left,
        tc_plot_item3d_handle right) {
        return left.scene_id == right.scene_id &&
               left.index == right.index &&
               left.generation == right.generation;
    }

    void rebuild(Slot& slot) {
        auto engine = std::make_unique<tcplot::PlotEngine3D>();
        engine->show_grid = slot.kind == TC_PLOT_ITEM3D_GRID;
        engine->show_series = slot.kind != TC_PLOT_ITEM3D_GRID;
        engine->show_labels =
            slot.kind == TC_PLOT_ITEM3D_GRID &&
            slot.grid_style.labels_visible != 0;

        if (slot.kind == TC_PLOT_ITEM3D_SURFACE) {
            const auto& style = slot.surface_style;
            tcplot::SurfacePlotOptions options;
            options.color = tcplot::Color4{
                style.color_r, style.color_g, style.color_b, style.color_a};
            options.colormap = colormap(style.colormap);
            options.colormap_reversed = style.colormap_reversed != 0;
            options.wireframe = style.wireframe != 0;
            engine->surface(
                slot.x, slot.y, slot.z,
                slot.rows, slot.columns, options);
            tcplot::SurfaceGridOptions grid;
            grid.visible = style.surface_grid_visible != 0;
            grid.row_step = style.surface_grid_row_step;
            grid.col_step = style.surface_grid_col_step;
            grid.width_px = style.surface_grid_width_px;
            grid.color = tcplot::Color4{
                style.surface_grid_r,
                style.surface_grid_g,
                style.surface_grid_b,
                style.surface_grid_a};
            if (!engine->set_surface_grid(0, grid)) {
                throw std::runtime_error("failed to apply retained surface grid");
            }
        } else if (slot.kind == TC_PLOT_ITEM3D_SCATTER) {
            const auto& style = slot.scatter_style;
            tcplot::ScatterPlotOptions options;
            options.color = tcplot::Color4{
                style.color_r, style.color_g, style.color_b, style.color_a};
            options.size = style.size;
            engine->scatter(slot.x, slot.y, slot.z, options);
        } else if (slot.kind == TC_PLOT_ITEM3D_GRID) {
            double lo[3], hi[3];
            bounds(lo, hi);
            tcplot::LinePlotOptions options;
            options.color = tcplot::Color4{0, 0, 0, 0};
            engine->plot(
                {lo[0], hi[0]},
                {lo[1], hi[1]},
                {lo[2], hi[2]},
                options);
            const auto& style = slot.grid_style;
            engine->grid_color = {
                style.grid_r, style.grid_g, style.grid_b, style.grid_a};
            engine->axis_colors = {
                tcplot::Color4{
                    style.x_axis_r, style.x_axis_g, style.x_axis_b, 1},
                tcplot::Color4{
                    style.y_axis_r, style.y_axis_g, style.y_axis_b, 1},
                tcplot::Color4{
                    style.z_axis_r, style.z_axis_g, style.z_axis_b, 1},
            };
        }
        slot.engine = std::move(engine);
        ++slot.render_revision;
        slot.gpu_revision = 0;
    }

    void apply_style(Slot& slot) {
        if (!slot.engine) {
            rebuild(slot);
            return;
        }

        tcplot::PlotEngine3D& engine = *slot.engine;
        if (slot.kind == TC_PLOT_ITEM3D_SURFACE) {
            const auto& style = slot.surface_style;
            tcplot::SurfaceGridOptions grid;
            grid.visible = style.surface_grid_visible != 0;
            grid.row_step = style.surface_grid_row_step;
            grid.col_step = style.surface_grid_col_step;
            grid.width_px = style.surface_grid_width_px;
            grid.color = tcplot::Color4{
                style.surface_grid_r,
                style.surface_grid_g,
                style.surface_grid_b,
                style.surface_grid_a};
            if (!engine.set_surface_color(
                    0,
                    {style.color_r, style.color_g,
                     style.color_b, style.color_a}) ||
                !engine.set_surface_colormap(0, colormap(style.colormap)) ||
                !engine.set_surface_colormap_reversed(
                    0, style.colormap_reversed != 0) ||
                !engine.set_surface_wireframe(0, style.wireframe != 0) ||
                !engine.set_surface_grid(0, grid)) {
                throw std::runtime_error(
                    "failed to apply retained surface style");
            }
        } else if (slot.kind == TC_PLOT_ITEM3D_SCATTER) {
            const auto& style = slot.scatter_style;
            if (!engine.set_scatter_style(
                    0,
                    {style.color_r, style.color_g,
                     style.color_b, style.color_a},
                    style.size)) {
                throw std::runtime_error(
                    "failed to apply retained scatter style");
            }
        } else if (slot.kind == TC_PLOT_ITEM3D_GRID) {
            const auto& style = slot.grid_style;
            engine.show_labels = style.labels_visible != 0;
            engine.set_grid_style(
                {style.grid_r, style.grid_g,
                 style.grid_b, style.grid_a},
                {
                    tcplot::Color4{
                        style.x_axis_r, style.x_axis_g, style.x_axis_b, 1},
                    tcplot::Color4{
                        style.y_axis_r, style.y_axis_g, style.y_axis_b, 1},
                    tcplot::Color4{
                        style.z_axis_r, style.z_axis_g, style.z_axis_b, 1},
                });
        }
        slot.gpu_revision = 0;
    }

    void bounds(double lo[3], double hi[3]) const {
        for (int axis = 0; axis < 3; ++axis) {
            lo[axis] = std::numeric_limits<double>::infinity();
            hi[axis] = -std::numeric_limits<double>::infinity();
        }
        bool found = false;
        for (const Slot& slot : slots_) {
            if (!slot.alive ||
                (slot.kind != TC_PLOT_ITEM3D_SURFACE &&
                 slot.kind != TC_PLOT_ITEM3D_SCATTER)) {
                continue;
            }
            for (std::size_t index = 0; index < slot.x.size(); ++index) {
                const double values[3] = {
                    slot.x[index], slot.y[index], slot.z[index]};
                for (int axis = 0; axis < 3; ++axis) {
                    lo[axis] = std::min(lo[axis], values[axis]);
                    hi[axis] = std::max(hi[axis], values[axis]);
                }
                found = true;
            }
        }
        if (!found) {
            lo[0] = lo[1] = lo[2] = 0.0;
            hi[0] = hi[1] = hi[2] = 1.0;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (hi[axis] - lo[axis] <= 1e-12) {
                lo[axis] -= 0.5;
                hi[axis] += 0.5;
            }
        }
    }

    void synchronize_grids() {
        if (!bounds_dirty_) return;
        for (Slot& slot : slots_) {
            if (!slot.alive || slot.kind != TC_PLOT_ITEM3D_GRID) continue;
            rebuild(slot);
        }
        bounds_dirty_ = false;
    }

    void render_slot(
        Slot& slot,
        int width,
        int height,
        tgfx::FontAtlas* font) {
        if (!slot.engine) rebuild(slot);
        tcplot::PlotEngine3D& engine = *slot.engine;
        engine.camera = camera_;
        engine.x_scale = axis_scale_[0];
        engine.y_scale = axis_scale_[1];
        engine.z_scale = axis_scale_[2];
        engine.surface_shading = shading_;
        engine.surface_shading_strength = shading_strength_;
        engine.surface_light_dir = light_;
        engine.data.x_label = x_label_;
        engine.data.y_label = y_label_;
        engine.data.z_label = z_label_;
        engine.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
        engine.render(&host_->ctx(), font);
        slot.gpu_revision = slot.render_revision;
    }

    void ensure_offscreen(int width, int height) {
        if (width_ == width && height_ == height &&
            color_.id != 0 && depth_.id != 0) {
            return;
        }
        if (color_.id != 0) host_->device().destroy(color_);
        if (depth_.id != 0) host_->device().destroy(depth_);

        tgfx::TextureDesc color_desc;
        color_desc.width = static_cast<std::uint32_t>(width);
        color_desc.height = static_cast<std::uint32_t>(height);
        color_desc.format = tgfx::PixelFormat::RGBA8_UNorm;
        color_desc.usage = tgfx::TextureUsage::Sampled |
                           tgfx::TextureUsage::ColorAttachment |
                           tgfx::TextureUsage::CopySrc;
        color_desc.sample_count =
            static_cast<std::uint32_t>(msaa_samples_);
        color_ = host_->device().create_texture(color_desc);

        tgfx::TextureDesc depth_desc;
        depth_desc.width = static_cast<std::uint32_t>(width);
        depth_desc.height = static_cast<std::uint32_t>(height);
        depth_desc.format = tgfx::PixelFormat::D24_UNorm;
        depth_desc.usage = tgfx::TextureUsage::DepthStencilAttachment;
        depth_desc.sample_count =
            static_cast<std::uint32_t>(msaa_samples_);
        depth_ = host_->device().create_texture(depth_desc);
        if (color_.id == 0 || depth_.id == 0) {
            throw std::runtime_error(
                "failed to create retained Chart3D attachments");
        }
        width_ = width;
        height_ = height;
    }

    void release_targets() {
        if (host_) {
            if (color_.id != 0) host_->device().destroy(color_);
            if (depth_.id != 0) host_->device().destroy(depth_);
        }
        color_ = {};
        depth_ = {};
        width_ = 0;
        height_ = 0;
    }

    tcplot::GpuHost* host_;
    std::uint64_t scene_id_;
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
    tc_plot_item3d_handle grid_part_ = invalid_item();
    tcplot::OrbitCamera camera_;
    bool shading_ = true;
    float shading_strength_ = 0.38f;
    termin::Vec3f light_{-0.4f, -0.6f, 0.7f};
    float axis_scale_[3] = {1, 1, 1};
    std::string x_label_ = "x";
    std::string y_label_ = "y";
    std::string z_label_ = "z";
    bool bounds_dirty_ = true;
    bool dragging_ = false;
    int drag_button_ = 0;
    float drag_x_ = 0;
    float drag_y_ = 0;
    tgfx::TextureHandle color_{};
    tgfx::TextureHandle depth_{};
    int width_ = 0;
    int height_ = 0;
    int msaa_samples_ = 4;
};

const char* PlotScene3DRenderItemSource::source_name() const noexcept {
    return "PlotScene3DRenderItemSource";
}

bool PlotScene3DRenderItemSource::collect_items(
    const termin::RenderItemSourceRequest&,
    termin::RenderItemCollection& output,
    termin::RenderItemSnapshotCounters& counters) {
    if (!scene_) {
        tc::Log::error(
            "[PlotScene3DRenderItemSource] retained chart scene is unavailable");
        return false;
    }

    counters.source_traversals = 1;
    for (const RetainedChart3D::Slot& slot : scene_->slots_) {
        if (!slot.alive) {
            continue;
        }

        uint32_t render_kind = TC_RENDER_ITEM_KIND_INVALID;
        switch (slot.kind) {
            case TC_PLOT_ITEM3D_SURFACE:
                render_kind = tcplot::PLOT_RENDER_ITEM_KIND_SURFACE;
                break;
            case TC_PLOT_ITEM3D_SCATTER:
                render_kind = tcplot::PLOT_RENDER_ITEM_KIND_SCATTER;
                break;
            case TC_PLOT_ITEM3D_GRID:
                render_kind = tcplot::PLOT_RENDER_ITEM_KIND_GRID;
                break;
            default:
                tc::Log::error(
                    "[PlotScene3DRenderItemSource] unsupported retained item kind %u",
                    static_cast<unsigned>(slot.kind));
                return false;
        }

        tc_render_item item{};
        item.kind = render_kind;
        item.source.domain_id = tcplot::PLOT_RENDER_ITEM_SOURCE_DOMAIN;
        item.source.namespace_id = scene_->scene_id_;
        item.source.object_id = slot.index;
        item.source.generation = slot.generation;
        item.source.subobject_id = 0;
        item.source.adapter_data = 0;
        output.items.push_back(item);
        ++counters.producers;
    }
    return true;
}

template <typename Result, typename Function>
Result logged(const char* operation, Result failure, Function&& function) {
    try {
        return function();
    } catch (const std::exception& error) {
        tc::Log::error(
            "RetainedChart3D: %s failed: %s", operation, error.what());
    } catch (...) {
        tc::Log::error(
            "RetainedChart3D: %s failed with an unknown exception", operation);
    }
    return failure;
}

}  // namespace

struct tc_retained_chart3d {
    explicit tc_retained_chart3d(tcplot::GpuHost& host)
        : value(host), render_item_source(value) {}
    RetainedChart3D value;
    PlotScene3DRenderItemSource render_item_source;
};

namespace tcplot {

termin::RenderItemSource& plot_scene3d_render_item_source(
    tc_retained_chart3d& chart) {
    return chart.render_item_source;
}

} // namespace tcplot

extern "C" {

tc_retained_chart3d* tc_retained_chart3d_create(void* gpu_host) {
    if (!gpu_host) {
        tc::Log::error("RetainedChart3D: create requires a live GpuHost");
        return nullptr;
    }
    return logged(
        "create",
        static_cast<tc_retained_chart3d*>(nullptr),
        [&] {
            return new tc_retained_chart3d(
                *static_cast<tcplot::GpuHost*>(gpu_host));
        });
}

void tc_retained_chart3d_destroy(tc_retained_chart3d* chart) {
    if (!chart) return;
    logged("destroy", false, [&] { delete chart; return true; });
}

uint64_t tc_retained_chart3d_scene_id(const tc_retained_chart3d* chart) {
    return chart ? chart->value.scene_id() : 0;
}

size_t tc_retained_chart3d_item_count(const tc_retained_chart3d* chart) {
    return chart ? chart->value.item_count() : 0;
}

int tc_retained_chart3d_item_is_valid(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle item) {
    return chart && chart->value.valid(item) ? 1 : 0;
}

int tc_retained_chart3d_item_snapshot(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle item,
    tc_plot_item3d_snapshot* snapshot) {
    return chart && snapshot && chart->value.snapshot(item, *snapshot) ? 1 : 0;
}

int tc_retained_chart3d_destroy_item(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle item) {
    return chart && chart->value.destroy(item) ? 1 : 0;
}

tc_plot_item3d_handle tc_retained_chart3d_add_surface(
    tc_retained_chart3d* chart,
    const double* x,
    const double* y,
    const double* z,
    uint32_t rows,
    uint32_t columns,
    const tc_surface_item3d_style* style) {
    if (!chart) return invalid_item();
    const auto resolved = style ? *style : default_surface_style();
    return logged(
        "add_surface", invalid_item(),
        [&] { return chart->value.add_surface(x, y, z, rows, columns, resolved); });
}

int tc_retained_chart3d_surface_set_data(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    const double* x,
    const double* y,
    const double* z,
    uint32_t rows,
    uint32_t columns) {
    if (!chart) return 0;
    return logged("surface_set_data", 0, [&] {
        return chart->value.set_surface_data(
            surface, x, y, z, rows, columns) ? 1 : 0;
    });
}

int tc_retained_chart3d_surface_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    const tc_surface_item3d_style* style) {
    if (!chart || !style) return 0;
    return logged(
        "surface_set_style", 0,
        [&] { return chart->value.set_surface_style(surface, *style) ? 1 : 0; });
}

int tc_retained_chart3d_surface_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle surface,
    tc_surface_item3d_style* style) {
    return chart && style && chart->value.surface_style(surface, *style) ? 1 : 0;
}

tc_plot_item3d_handle tc_retained_chart3d_add_scatter(
    tc_retained_chart3d* chart,
    const double* x,
    const double* y,
    const double* z,
    size_t count,
    const tc_scatter_item3d_style* style) {
    if (!chart) return invalid_item();
    const auto resolved = style ? *style : default_scatter_style();
    return logged(
        "add_scatter", invalid_item(),
        [&] { return chart->value.add_scatter(x, y, z, count, resolved); });
}

int tc_retained_chart3d_scatter_set_data(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    const double* x,
    const double* y,
    const double* z,
    size_t count) {
    if (!chart) return 0;
    return logged("scatter_set_data", 0, [&] {
        return chart->value.set_scatter_data(
            scatter, x, y, z, count) ? 1 : 0;
    });
}

int tc_retained_chart3d_scatter_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    const tc_scatter_item3d_style* style) {
    if (!chart || !style) return 0;
    return logged(
        "scatter_set_style", 0,
        [&] { return chart->value.set_scatter_style(scatter, *style) ? 1 : 0; });
}

int tc_retained_chart3d_scatter_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle scatter,
    tc_scatter_item3d_style* style) {
    return chart && style && chart->value.scatter_style(scatter, *style) ? 1 : 0;
}

tc_plot_item3d_handle tc_retained_chart3d_add_grid(
    tc_retained_chart3d* chart,
    const tc_grid_item3d_style* style) {
    if (!chart) return invalid_item();
    const auto resolved = style ? *style : default_grid_style();
    return logged(
        "add_grid", invalid_item(),
        [&] { return chart->value.add_grid(resolved); });
}

int tc_retained_chart3d_grid_set_style(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid,
    const tc_grid_item3d_style* style) {
    if (!chart || !style) return 0;
    return logged(
        "grid_set_style", 0,
        [&] { return chart->value.set_grid_style(grid, *style) ? 1 : 0; });
}

int tc_retained_chart3d_grid_get_style(
    const tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid,
    tc_grid_item3d_style* style) {
    return chart && style && chart->value.grid_style(grid, *style) ? 1 : 0;
}

tc_plot_item3d_handle tc_retained_chart3d_grid_part(
    const tc_retained_chart3d* chart) {
    return chart ? chart->value.grid_part() : invalid_item();
}

int tc_retained_chart3d_set_grid_part(
    tc_retained_chart3d* chart,
    tc_plot_item3d_handle grid) {
    return chart && chart->value.set_grid_part(grid) ? 1 : 0;
}

void tc_retained_chart3d_set_axis_labels(
    tc_retained_chart3d* chart,
    const char* x_label,
    const char* y_label,
    const char* z_label) {
    if (!chart) return;
    chart->value.set_axis_labels(
        x_label ? x_label : "",
        y_label ? y_label : "",
        z_label ? z_label : "");
}

int tc_retained_chart3d_set_surface_shading(
    tc_retained_chart3d* chart,
    int enabled,
    float strength) {
    if (!chart) return 0;
    return logged("set_surface_shading", 0, [&] {
        chart->value.set_shading(enabled != 0, strength);
        return 1;
    });
}

int tc_retained_chart3d_set_light_direction(
    tc_retained_chart3d* chart,
    float x,
    float y,
    float z) {
    if (!chart) return 0;
    return logged("set_light_direction", 0, [&] {
        chart->value.set_light(x, y, z);
        return 1;
    });
}

int tc_retained_chart3d_set_axis_scale(
    tc_retained_chart3d* chart,
    float x,
    float y,
    float z) {
    if (!chart) return 0;
    return logged("set_axis_scale", 0, [&] {
        chart->value.set_axis_scale(x, y, z);
        return 1;
    });
}

int tc_retained_chart3d_set_msaa_samples(
    tc_retained_chart3d* chart,
    int samples) {
    if (!chart) return 0;
    return logged("set_msaa_samples", 0, [&] {
        chart->value.set_msaa_samples(samples);
        return 1;
    });
}

int tc_retained_chart3d_msaa_samples(
    const tc_retained_chart3d* chart) {
    return chart ? chart->value.msaa_samples() : 0;
}

int tc_retained_chart3d_get_camera(
    const tc_retained_chart3d* chart,
    tc_orbit_camera3d_state* state) {
    if (!chart || !state) return 0;
    *state = chart->value.camera_state();
    return 1;
}

int tc_retained_chart3d_set_camera(
    tc_retained_chart3d* chart,
    const tc_orbit_camera3d_state* state) {
    if (!chart || !state) return 0;
    return logged("set_camera", 0, [&] {
        chart->value.set_camera(*state);
        return 1;
    });
}

void tc_retained_chart3d_fit_camera(tc_retained_chart3d* chart) {
    if (chart) chart->value.fit_camera();
}

void tc_retained_chart3d_reset_camera(tc_retained_chart3d* chart) {
    if (chart) chart->value.reset_camera();
}

int tc_retained_chart3d_pointer_down(
    tc_retained_chart3d* chart,
    float x,
    float y,
    int button) {
    return chart && chart->value.pointer_down(x, y, button) ? 1 : 0;
}

void tc_retained_chart3d_pointer_move(
    tc_retained_chart3d* chart,
    float x,
    float y) {
    if (chart) chart->value.pointer_move(x, y);
}

void tc_retained_chart3d_pointer_up(
    tc_retained_chart3d* chart,
    float,
    float,
    int) {
    if (chart) chart->value.pointer_up();
}

int tc_retained_chart3d_wheel(
    tc_retained_chart3d* chart,
    float,
    float,
    float delta) {
    return chart && chart->value.wheel(delta) ? 1 : 0;
}

uint32_t tc_retained_chart3d_render(
    tc_retained_chart3d* chart,
    int width,
    int height) {
    if (!chart) return 0;
    return logged(
        "render", uint32_t{0},
        [&] { return chart->value.render(width, height); });
}

void tc_retained_chart3d_release_gpu(tc_retained_chart3d* chart) {
    if (!chart) return;
    logged("release_gpu", false, [&] {
        chart->value.release_gpu();
        return true;
    });
}

}  // extern "C"
