#include "tcplot/plot_projection2d.hpp"

#include <cmath>

#include <tcbase/tc_log.h>
#include <tcbase/tc_pool.h>

namespace {

    struct ProjectionSlot {
        tc_visual_scene_handle owner_scene = tc_visual_scene_handle_invalid();
        std::uint64_t owner_scene_id = 0;
        tc_plot_projection_state2d state{};
    };

    tc_pool g_projection_pool{};
    bool g_projection_pool_initialized = false;

    bool finite_rect(tc_plot_rect2d value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.width) &&
               std::isfinite(value.height);
    }

    tc_handle local_handle(tc_plot_projection_handle2d handle) {
        return {handle.index, handle.generation};
    }

    bool ensure_pool() {
        if (g_projection_pool_initialized)
            return true;
        const tc_pool_config config = {
            .max_capacity = 0,
            .initial_generation = 1,
            .allocate_low_indices_first = true,
            .name = "PlotProjection2D",
        };
        if (!tc_pool_init_ex(&g_projection_pool, sizeof(ProjectionSlot), 16, &config)) {
            tc_log_error("tc_plot_projection2d: pool initialization failed");
            return false;
        }
        g_projection_pool_initialized = true;
        return true;
    }

    ProjectionSlot* raw_slot(tc_plot_projection_handle2d handle) {
        if (!g_projection_pool_initialized || tc_plot_projection_handle2d_is_invalid(handle)) {
            return nullptr;
        }
        auto* slot = static_cast<ProjectionSlot*>(tc_pool_get(&g_projection_pool, local_handle(handle)));
        return slot != nullptr && slot->owner_scene_id == handle.scene_id ? slot : nullptr;
    }

    const ProjectionSlot* live_slot(tc_plot_projection_handle2d handle) {
        const ProjectionSlot* slot = raw_slot(handle);
        if (slot == nullptr || !tc_visual_scene_is_valid(slot->owner_scene) ||
            tc_visual_scene_id(slot->owner_scene) != slot->owner_scene_id) {
            return nullptr;
        }
        return slot;
    }

    ProjectionSlot* checked_live_slot(tc_plot_projection_handle2d handle, const char* operation) {
        ProjectionSlot* slot = raw_slot(handle);
        if (slot == nullptr) {
            tc_log_error("%s: invalid or stale projection handle "
                         "(scene=%llu index=%u generation=%u)",
                         operation,
                         static_cast<unsigned long long>(handle.scene_id),
                         handle.index,
                         handle.generation);
            return nullptr;
        }
        if (!tc_visual_scene_is_valid(slot->owner_scene) ||
            tc_visual_scene_id(slot->owner_scene) != slot->owner_scene_id) {
            tc_log_error("%s: owner scene is stale for projection "
                         "(scene=%llu index=%u generation=%u)",
                         operation,
                         static_cast<unsigned long long>(handle.scene_id),
                         handle.index,
                         handle.generation);
            return nullptr;
        }
        return slot;
    }

    tc_plot_rect2d to_c(const tcplot::PlotRect2D& value) {
        return {
            value.x(),
            value.y(),
            value.width(),
            value.height(),
        };
    }

    tc_plot_range2d to_c(const tcplot::PlotRange2D& value) {
        return {
            value.x_min(),
            value.x_max(),
            value.y_min(),
            value.y_max(),
        };
    }

    tc_plot_projection_desc2d to_c(const tcplot::PlotFrame2D& frame) {
        return {
            to_c(frame.viewport()),
            to_c(frame.plot_area()),
            to_c(frame.range()),
            to_c(frame.clip_rect()),
            frame.pixel_scale(),
        };
    }

    tcplot::PlotRect2D from_c(tc_plot_rect2d value) {
        return {value.x, value.y, value.width, value.height};
    }

    tcplot::PlotRange2D from_c(tc_plot_range2d value) {
        return {
            value.x_min,
            value.x_max,
            value.y_min,
            value.y_max,
        };
    }

    tcplot::PlotFrame2D from_c(const tc_plot_projection_desc2d& value) {
        return {
            from_c(value.viewport),
            from_c(value.plot_area),
            from_c(value.range),
            from_c(value.clip_rect),
            value.pixel_scale,
        };
    }

} // namespace

extern "C" {

bool tc_plot_projection_desc2d_is_valid(const tc_plot_projection_desc2d* desc) {
    if (desc == nullptr || !finite_rect(desc->viewport) || !finite_rect(desc->plot_area) ||
        !finite_rect(desc->clip_rect) || !std::isfinite(desc->range.x_min) || !std::isfinite(desc->range.x_max) ||
        !std::isfinite(desc->range.y_min) || !std::isfinite(desc->range.y_max) || !std::isfinite(desc->pixel_scale)) {
        return false;
    }
    return desc->viewport.width >= 0.0f && desc->viewport.height >= 0.0f && desc->plot_area.width > 0.0f &&
           desc->plot_area.height > 0.0f && desc->clip_rect.width >= 0.0f && desc->clip_rect.height >= 0.0f &&
           desc->range.x_max > desc->range.x_min && desc->range.y_max > desc->range.y_min && desc->pixel_scale > 0.0f;
}

tc_plot_projection_handle2d tc_plot_projection2d_create(tc_visual_scene_handle owner_scene,
                                                        const tc_plot_projection_desc2d* desc) {
    if (!tc_visual_scene_is_valid(owner_scene)) {
        tc_log_error("tc_plot_projection2d_create: owner scene is invalid");
        return tc_plot_projection_handle2d_invalid();
    }
    if (!tc_plot_projection_desc2d_is_valid(desc)) {
        tc_log_error("tc_plot_projection2d_create: projection state is invalid");
        return tc_plot_projection_handle2d_invalid();
    }
    if (!ensure_pool()) {
        return tc_plot_projection_handle2d_invalid();
    }
    const tc_handle local = tc_pool_alloc(&g_projection_pool);
    if (tc_handle_is_invalid(local)) {
        tc_log_error("tc_plot_projection2d_create: pool allocation failed");
        return tc_plot_projection_handle2d_invalid();
    }
    auto* slot = static_cast<ProjectionSlot*>(tc_pool_get(&g_projection_pool, local));
    slot->owner_scene = owner_scene;
    slot->owner_scene_id = tc_visual_scene_id(owner_scene);
    slot->state.projection = *desc;
    slot->state.revision = 1;
    return {
        slot->owner_scene_id,
        local.index,
        local.generation,
    };
}

bool tc_plot_projection2d_destroy(tc_plot_projection_handle2d handle) {
    ProjectionSlot* slot = raw_slot(handle);
    if (slot == nullptr) {
        tc_log_error("tc_plot_projection2d_destroy: invalid or stale projection "
                     "handle (scene=%llu index=%u generation=%u)",
                     static_cast<unsigned long long>(handle.scene_id),
                     handle.index,
                     handle.generation);
        return false;
    }
    *slot = ProjectionSlot{};
    if (!tc_pool_free_slot(&g_projection_pool, local_handle(handle))) {
        tc_log_error("tc_plot_projection2d_destroy: failed to release projection "
                     "slot");
        return false;
    }
    return true;
}

bool tc_plot_projection2d_is_valid(tc_plot_projection_handle2d handle) {
    return live_slot(handle) != nullptr;
}

bool tc_plot_projection2d_matches_scene(tc_plot_projection_handle2d handle, tc_visual_scene_handle scene) {
    const ProjectionSlot* slot = live_slot(handle);
    return slot != nullptr && tc_visual_scene_is_valid(scene) && tc_visual_scene_id(scene) == slot->owner_scene_id &&
           tc_visual_scene_handle_eq(scene, slot->owner_scene);
}

tc_visual_scene_handle tc_plot_projection2d_owner_scene(tc_plot_projection_handle2d handle) {
    const ProjectionSlot* slot = live_slot(handle);
    return slot != nullptr ? slot->owner_scene : tc_visual_scene_handle_invalid();
}

bool tc_plot_projection2d_update(tc_plot_projection_handle2d handle, const tc_plot_projection_desc2d* desc) {
    ProjectionSlot* slot = checked_live_slot(handle, "tc_plot_projection2d_update");
    if (slot == nullptr)
        return false;
    if (!tc_plot_projection_desc2d_is_valid(desc)) {
        tc_log_error("tc_plot_projection2d_update: projection state is invalid");
        return false;
    }
    slot->state.projection = *desc;
    ++slot->state.revision;
    if (slot->state.revision == 0) {
        slot->state.revision = 1;
    }
    return true;
}

bool tc_plot_projection2d_snapshot(tc_plot_projection_handle2d handle, tc_plot_projection_state2d* out_state) {
    if (out_state == nullptr) {
        tc_log_error("tc_plot_projection2d_snapshot: output is required");
        return false;
    }
    const ProjectionSlot* slot = checked_live_slot(handle, "tc_plot_projection2d_snapshot");
    if (slot == nullptr)
        return false;
    *out_state = slot->state;
    return true;
}

bool tc_plot_projection_state2d_data_to_visual(const tc_plot_projection_state2d* state,
                                               tc_plot_point2d data,
                                               tc_plot_visual_point2d* out_visual) {
    if (state == nullptr || out_visual == nullptr || !tc_plot_projection_desc2d_is_valid(&state->projection) ||
        !std::isfinite(data.x) || !std::isfinite(data.y)) {
        tc_log_error("tc_plot_projection_state2d_data_to_visual: "
                     "valid state, input and output are required");
        return false;
    }
    const tc_plot_projection_desc2d& value = state->projection;
    const double sx = (data.x - value.range.x_min) / (value.range.x_max - value.range.x_min);
    const double sy = (data.y - value.range.y_min) / (value.range.y_max - value.range.y_min);
    out_visual->x = value.plot_area.x + static_cast<float>(sx) * value.plot_area.width;
    out_visual->y = value.plot_area.y + (1.0f - static_cast<float>(sy)) * value.plot_area.height;
    return true;
}

bool tc_plot_projection_state2d_visual_to_data(const tc_plot_projection_state2d* state,
                                               tc_plot_visual_point2d visual,
                                               tc_plot_point2d* out_data) {
    if (state == nullptr || out_data == nullptr || !tc_plot_projection_desc2d_is_valid(&state->projection) ||
        !std::isfinite(visual.x) || !std::isfinite(visual.y)) {
        tc_log_error("tc_plot_projection_state2d_visual_to_data: "
                     "valid state, input and output are required");
        return false;
    }
    const tc_plot_projection_desc2d& value = state->projection;
    const double sx = (visual.x - value.plot_area.x) / value.plot_area.width;
    const double sy = 1.0 - (visual.y - value.plot_area.y) / value.plot_area.height;
    out_data->x = value.range.x_min + sx * (value.range.x_max - value.range.x_min);
    out_data->y = value.range.y_min + sy * (value.range.y_max - value.range.y_min);
    return true;
}

} // extern "C"

namespace tcplot {

    bool PlotProjection2D::valid() const {
        return tc_plot_projection2d_is_valid(handle_);
    }

    bool PlotProjection2D::matches_scene(const termin::visual::TcVisualScene& scene) const {
        return tc_plot_projection2d_matches_scene(handle_, scene.handle());
    }

    termin::visual::TcVisualScene PlotProjection2D::owner_scene() const {
        return termin::visual::TcVisualScene{tc_plot_projection2d_owner_scene(handle_)};
    }

    bool PlotProjection2D::update(const PlotFrame2D& frame) {
        const tc_plot_projection_desc2d desc = to_c(frame);
        return tc_plot_projection2d_update(handle_, &desc);
    }

    std::optional<PlotProjectionSnapshot2D> PlotProjection2D::snapshot() const {
        tc_plot_projection_state2d state{};
        if (!tc_plot_projection2d_snapshot(handle_, &state)) {
            return std::nullopt;
        }
        return PlotProjectionSnapshot2D{
            from_c(state.projection),
            state.revision,
        };
    }

    std::optional<PlotProjection2D> create_plot_projection2d(const termin::visual::TcVisualScene& owner_scene,
                                                             const PlotFrame2D& frame) {
        const tc_plot_projection_desc2d desc = to_c(frame);
        const PlotProjectionHandle2D handle = tc_plot_projection2d_create(owner_scene.handle(), &desc);
        if (tc_plot_projection_handle2d_is_invalid(handle)) {
            return std::nullopt;
        }
        return PlotProjection2D{handle};
    }

    bool destroy_plot_projection2d(PlotProjection2D projection) {
        return tc_plot_projection2d_destroy(projection.handle());
    }

} // namespace tcplot
