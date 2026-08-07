#include "tcplot/retained_multi_chart2d.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <termin_visual_scene/tc_visual_scene_item2d.h>

#include "tcplot/gpu_host.hpp"

namespace
{
    std::atomic<uint64_t> next_multi_chart_id{1};

    bool valid_rect(tc_plot_rect2d rect)
    {
        return std::isfinite(rect.x) && std::isfinite(rect.y) &&
               std::isfinite(rect.width) && rect.width > 0.0f &&
               std::isfinite(rect.height) && rect.height > 0.0f;
    }

    bool valid_range(tc_plot_range2d range)
    {
        return std::isfinite(range.x_min) && std::isfinite(range.x_max) &&
               std::isfinite(range.y_min) && std::isfinite(range.y_max) &&
               range.x_max > range.x_min && range.y_max > range.y_min;
    }

    tc_rect2f visual_rect(tc_plot_rect2d rect)
    {
        return {rect.x, rect.y, rect.width, rect.height};
    }

    tc_multi_chart_panel_handle2d invalid_panel()
    {
        return {0, UINT32_MAX, 0};
    }

    class RetainedMultiChart2D
    {
    public:
        RetainedMultiChart2D(tcplot::GpuHost& host,
                             tc_plot_rect2d viewport,
                             tc_plot_range2d initial_range,
                             size_t panel_count,
                             float panel_height,
                             float panel_gap,
                             std::string font_uri,
                             float pixel_scale,
                             tc_chart2d_theme theme)
            : host_(&host), viewport_(viewport), initial_range_(initial_range),
              shared_x_min_(initial_range.x_min),
              shared_x_max_(initial_range.x_max),
              panel_height_(panel_height), panel_gap_(panel_gap),
              font_uri_(std::move(font_uri)), pixel_scale_(pixel_scale),
              theme_(theme), id_(next_multi_chart_id.fetch_add(1))
        {
            if (!valid_rect(viewport_) || !valid_range(initial_range_) ||
                panel_count > std::numeric_limits<uint32_t>::max() ||
                !std::isfinite(panel_height_) || panel_height_ < 0.0f ||
                !std::isfinite(panel_gap_) || panel_gap_ < 0.0f ||
                font_uri_.empty() || !std::isfinite(pixel_scale_) ||
                pixel_scale_ <= 0.0f)
                throw std::invalid_argument(
                    "RetainedMultiChart2D configuration is invalid");
            scene_ = tc_visual_scene_create();
            if (!tc_visual_scene_is_valid(scene_))
                throw std::runtime_error(
                    "failed to create multi-chart visual scene");
            try
            {
                root_ = tc_visual_group_item2d_create(
                    scene_, tc_graphic_item_handle_invalid());
                if (tc_graphic_item_handle_is_invalid(root_))
                    throw std::runtime_error(
                        "failed to create multi-chart root");
                if (!tc_visual_scene_item_set_clip_rect(
                        scene_, root_, visual_rect(viewport_)))
                    throw std::runtime_error(
                        "failed to clip multi-chart root");
                set_panel_count(panel_count);
            }
            catch (...)
            {
                cleanup();
                throw;
            }
        }

        ~RetainedMultiChart2D()
        {
            cleanup();
        }

        tc_visual_scene_handle scene() const
        {
            return scene_;
        }

        tc_graphic_item_handle root() const
        {
            return root_;
        }

        tc_retained_multi_chart2d_state snapshot() const
        {
            return {
                viewport_,
                {shared_x_min_, shared_x_max_, 0.0, 1.0},
                pixel_scale_,
                panel_height_,
                panel_gap_,
                scroll_offset_,
                total_virtual_height(),
                maximum_scroll_offset(),
                panel_count_,
                layout_revision_,
            };
        }

        void set_viewport(tc_plot_rect2d viewport, float pixel_scale)
        {
            if (!valid_rect(viewport) || !std::isfinite(pixel_scale) ||
                pixel_scale <= 0.0f)
                throw std::invalid_argument(
                    "multi-chart viewport is invalid");
            viewport_ = viewport;
            pixel_scale_ = pixel_scale;
            if (!tc_visual_scene_item_set_clip_rect(
                    scene_, root_, visual_rect(viewport_)))
                throw std::runtime_error(
                    "failed to update multi-chart clipping");
            apply_layout(true);
        }

        void set_panel_count(size_t panel_count)
        {
            if (panel_count > std::numeric_limits<uint32_t>::max())
                throw std::invalid_argument("multi-chart has too many panels");
            if (panel_count == panel_count_)
            {
                apply_layout(false);
                return;
            }

            const size_t old_count = panel_count_;
            if (panel_count < old_count)
            {
                for (size_t index = panel_count; index < old_count; ++index)
                    destroy_panel(index);
                panel_count_ = panel_count;
                apply_layout(false);
                return;
            }

            if (panels_.size() < panel_count)
                panels_.resize(panel_count);
            size_t created = old_count;
            try
            {
                for (; created < panel_count; ++created)
                    create_panel(created);
                panel_count_ = panel_count;
                apply_layout(false);
            }
            catch (...)
            {
                for (size_t index = old_count; index < created; ++index)
                    destroy_panel(index);
                panel_count_ = old_count;
                throw;
            }
        }

        void set_panel_layout(float panel_height, float panel_gap)
        {
            if (!std::isfinite(panel_height) || panel_height < 0.0f ||
                !std::isfinite(panel_gap) || panel_gap < 0.0f)
                throw std::invalid_argument(
                    "multi-chart panel layout is invalid");
            panel_height_ = panel_height;
            panel_gap_ = panel_gap;
            apply_layout(true);
        }

        void set_scroll_offset(float offset)
        {
            if (!std::isfinite(offset))
                throw std::invalid_argument(
                    "multi-chart scroll offset is invalid");
            scroll_offset_ = std::clamp(
                offset, 0.0f, maximum_scroll_offset());
            apply_layout(false);
        }

        void set_shared_x(double x_min, double x_max)
        {
            if (!std::isfinite(x_min) || !std::isfinite(x_max) ||
                x_max <= x_min)
                throw std::invalid_argument(
                    "multi-chart shared X range is invalid");
            shared_x_min_ = x_min;
            shared_x_max_ = x_max;
            for (size_t index = 0; index < panel_count_; ++index)
            {
                PanelSlot& panel = panels_[index];
                if (!panel.visible)
                    continue;
                tc_plot_range2d range{};
                if (!tc_retained_chart2d_range(panel.chart, &range))
                    throw std::runtime_error(
                        "failed to snapshot multi-chart panel range");
                if (range.x_min == x_min && range.x_max == x_max)
                    continue;
                range.x_min = x_min;
                range.x_max = x_max;
                if (!tc_retained_chart2d_set_range(panel.chart, range))
                    throw std::runtime_error(
                        "failed to update multi-chart shared X");
            }
        }

        void set_theme(tc_chart2d_theme theme)
        {
            for (size_t index = 0; index < panel_count_; ++index)
            {
                if (!tc_retained_chart2d_set_theme(
                        panels_[index].chart, &theme))
                    throw std::runtime_error(
                        "failed to update multi-chart theme");
            }
            theme_ = theme;
        }

        tc_multi_chart_panel_handle2d panel_at(size_t index) const
        {
            if (index >= panel_count_ || !panels_[index].alive)
                return invalid_panel();
            return panel_handle(panels_[index]);
        }

        bool panel_valid(tc_multi_chart_panel_handle2d panel) const
        {
            return resolve(panel) != nullptr;
        }

        tc_retained_chart2d* panel_chart(tc_multi_chart_panel_handle2d panel)
        {
            PanelSlot* slot = resolve(panel);
            return slot ? slot->chart : nullptr;
        }

    private:
        struct PanelSlot
        {
            uint32_t index = 0;
            uint32_t generation = 1;
            bool alive = false;
            bool visible = false;
            tc_plot_rect2d desired_viewport{};
            tc_retained_chart2d* chart = nullptr;
        };

        tc_multi_chart_panel_handle2d panel_handle(
            const PanelSlot& slot) const
        {
            return {id_, slot.index, slot.generation};
        }

        PanelSlot* resolve(tc_multi_chart_panel_handle2d panel)
        {
            if (panel.multi_chart_id != id_ ||
                panel.index >= panel_count_ ||
                panel.index >= panels_.size())
                return nullptr;
            PanelSlot& slot = panels_[panel.index];
            return slot.alive && slot.generation == panel.generation
                       ? &slot
                       : nullptr;
        }

        const PanelSlot* resolve(tc_multi_chart_panel_handle2d panel) const
        {
            return const_cast<RetainedMultiChart2D*>(this)->resolve(panel);
        }

        void create_panel(size_t index)
        {
            PanelSlot& panel = panels_[index];
            panel.index = static_cast<uint32_t>(index);
            const tc_plot_rect2d provisional = {
                viewport_.x, viewport_.y, viewport_.width, 1.0f};
            panel.chart = tc_retained_chart2d_create_in_scene(
                host_,
                scene_,
                provisional,
                {shared_x_min_,
                 shared_x_max_,
                 initial_range_.y_min,
                 initial_range_.y_max},
                font_uri_.c_str(),
                pixel_scale_,
                &theme_);
            if (!panel.chart)
                throw std::runtime_error(
                    "failed to create retained multi-chart panel");
            const tc_graphic_item_handle panel_root =
                tc_retained_chart2d_part_handle(
                    panel.chart, TC_CHART2D_PART_ROOT);
            const size_t child_index =
                tc_visual_scene_item_child_count(scene_, root_);
            if (!tc_visual_scene_item_set_parent(
                    scene_, panel_root, root_, child_index))
            {
                tc_retained_chart2d_destroy(panel.chart);
                panel.chart = nullptr;
                throw std::runtime_error(
                    "failed to adopt retained multi-chart panel");
            }
            panel.alive = true;
            panel.visible = false;
        }

        void destroy_panel(size_t index)
        {
            PanelSlot& panel = panels_[index];
            if (panel.chart)
                tc_retained_chart2d_destroy(panel.chart);
            panel.chart = nullptr;
            panel.alive = false;
            panel.visible = false;
            ++panel.generation;
            if (panel.generation == 0)
                ++panel.generation;
        }

        float resolved_panel_height() const
        {
            if (panel_count_ == 0)
                return 0.0f;
            if (panel_height_ > 0.0f)
                return panel_height_;
            const float gaps =
                panel_gap_ * static_cast<float>(panel_count_ - 1);
            return std::max(
                1.0f, (viewport_.height - gaps) / panel_count_);
        }

        float total_virtual_height() const
        {
            if (panel_count_ == 0)
                return 0.0f;
            return resolved_panel_height() * panel_count_ +
                   panel_gap_ * static_cast<float>(panel_count_ - 1);
        }

        float maximum_scroll_offset() const
        {
            return std::max(
                0.0f, total_virtual_height() - viewport_.height);
        }

        void apply_layout(bool force_visible_layout)
        {
            scroll_offset_ = std::clamp(
                scroll_offset_, 0.0f, maximum_scroll_offset());
            const float height = resolved_panel_height();
            const float viewport_bottom = viewport_.y + viewport_.height;
            for (size_t index = 0; index < panel_count_; ++index)
            {
                PanelSlot& panel = panels_[index];
                panel.desired_viewport = {
                    viewport_.x,
                    viewport_.y +
                        static_cast<float>(index) * (height + panel_gap_) -
                        scroll_offset_,
                    viewport_.width,
                    height,
                };
                const float panel_bottom =
                    panel.desired_viewport.y + panel.desired_viewport.height;
                const bool visible =
                    panel_bottom > viewport_.y &&
                    panel.desired_viewport.y < viewport_bottom;
                const tc_graphic_item_handle panel_root =
                    tc_retained_chart2d_part_handle(
                        panel.chart, TC_CHART2D_PART_ROOT);
                if (visible)
                {
                    tc_plot_range2d range{};
                    if (!tc_retained_chart2d_range(panel.chart, &range))
                        throw std::runtime_error(
                            "failed to snapshot virtualized panel range");
                    const bool frame_changed =
                        force_visible_layout || !panel.visible ||
                        panel.desired_viewport.y != panel_viewport_y(panel);
                    const bool x_changed = range.x_min != shared_x_min_ ||
                                           range.x_max != shared_x_max_;
                    range.x_min = shared_x_min_;
                    range.x_max = shared_x_max_;
                    const bool updated = frame_changed
                        ? tc_retained_chart2d_set_frame(panel.chart,
                                                       panel.desired_viewport,
                                                       pixel_scale_,
                                                       range)
                        : x_changed
                              ? tc_retained_chart2d_set_range(
                                    panel.chart, range)
                              : true;
                    if (!updated)
                        throw std::runtime_error(
                            "failed to layout retained multi-chart panel");
                }
                if (!tc_visual_scene_item_set_visible(
                        scene_, panel_root, visible))
                    throw std::runtime_error(
                        "failed to virtualize retained multi-chart panel");
                panel.visible = visible;
            }
            ++layout_revision_;
        }

        static float panel_viewport_y(const PanelSlot& panel)
        {
            tc_chart2d_layout layout{};
            return panel.chart &&
                           tc_retained_chart2d_layout(panel.chart, &layout)
                       ? layout.viewport.y
                       : std::numeric_limits<float>::quiet_NaN();
        }

        void cleanup()
        {
            for (PanelSlot& panel : panels_)
            {
                if (panel.chart)
                    tc_retained_chart2d_destroy(panel.chart);
                panel.chart = nullptr;
                panel.alive = false;
            }
            if (tc_visual_scene_is_valid(scene_))
                tc_visual_scene_destroy(scene_);
            scene_ = {};
            root_ = tc_graphic_item_handle_invalid();
        }

        tcplot::GpuHost* host_ = nullptr;
        tc_visual_scene_handle scene_{};
        tc_graphic_item_handle root_ = tc_graphic_item_handle_invalid();
        tc_plot_rect2d viewport_{};
        tc_plot_range2d initial_range_{};
        double shared_x_min_ = 0.0;
        double shared_x_max_ = 1.0;
        float panel_height_ = 0.0f;
        float panel_gap_ = 0.0f;
        float scroll_offset_ = 0.0f;
        std::string font_uri_;
        float pixel_scale_ = 1.0f;
        tc_chart2d_theme theme_{};
        uint64_t id_ = 0;
        size_t panel_count_ = 0;
        uint64_t layout_revision_ = 0;
        std::vector<PanelSlot> panels_;
    };

    template <typename Result, typename Function>
    Result logged(const char* operation, Result failure, Function&& function)
    {
        try
        {
            return function();
        }
        catch (const std::exception& error)
        {
            tc::Log::error("RetainedMultiChart2D: %s failed: %s",
                           operation,
                           error.what());
        }
        catch (...)
        {
            tc::Log::error(
                "RetainedMultiChart2D: %s failed with unknown error",
                operation);
        }
        return failure;
    }
}

struct tc_retained_multi_chart2d
{
    RetainedMultiChart2D value;

    tc_retained_multi_chart2d(tcplot::GpuHost& host,
                              tc_plot_rect2d viewport,
                              tc_plot_range2d initial_range,
                              size_t panel_count,
                              float panel_height,
                              float panel_gap,
                              std::string font_uri,
                              float pixel_scale,
                              tc_chart2d_theme theme)
        : value(host,
                viewport,
                initial_range,
                panel_count,
                panel_height,
                panel_gap,
                std::move(font_uri),
                pixel_scale,
                theme)
    {
    }
};

extern "C"
{
    tc_retained_multi_chart2d*
    tc_retained_multi_chart2d_create(void* gpu_host,
                                     tc_plot_rect2d viewport,
                                     tc_plot_range2d initial_range,
                                     size_t panel_count,
                                     float panel_height,
                                     float panel_gap,
                                     const char* font_uri,
                                     float pixel_scale,
                                     const tc_chart2d_theme* theme)
    {
        if (!gpu_host || !theme)
        {
            tc::Log::error(
                "RetainedMultiChart2D: create requires host and theme");
            return nullptr;
        }
        return logged("create",
                      static_cast<tc_retained_multi_chart2d*>(nullptr),
                      [&]
                      {
                          return new tc_retained_multi_chart2d(
                              *static_cast<tcplot::GpuHost*>(gpu_host),
                              viewport,
                              initial_range,
                              panel_count,
                              panel_height,
                              panel_gap,
                              font_uri ? font_uri : "",
                              pixel_scale,
                              *theme);
                      });
    }

    void tc_retained_multi_chart2d_destroy(tc_retained_multi_chart2d* chart)
    {
        delete chart;
    }

    tc_visual_scene_handle tc_retained_multi_chart2d_scene(
        const tc_retained_multi_chart2d* chart)
    {
        return chart ? chart->value.scene() : tc_visual_scene_handle{};
    }

    tc_graphic_item_handle tc_retained_multi_chart2d_root(
        const tc_retained_multi_chart2d* chart)
    {
        return chart ? chart->value.root() : tc_graphic_item_handle_invalid();
    }

    bool tc_retained_multi_chart2d_snapshot(
        const tc_retained_multi_chart2d* chart,
        tc_retained_multi_chart2d_state* out_snapshot)
    {
        if (!chart || !out_snapshot)
            return false;
        *out_snapshot = chart->value.snapshot();
        return true;
    }

    bool tc_retained_multi_chart2d_set_viewport(
        tc_retained_multi_chart2d* chart,
        tc_plot_rect2d viewport,
        float pixel_scale)
    {
        return chart && logged("set_viewport",
                               false,
                               [&]
                               {
                                   chart->value.set_viewport(
                                       viewport, pixel_scale);
                                   return true;
                               });
    }

    bool tc_retained_multi_chart2d_set_panel_count(
        tc_retained_multi_chart2d* chart, size_t panel_count)
    {
        return chart && logged("set_panel_count",
                               false,
                               [&]
                               {
                                   chart->value.set_panel_count(panel_count);
                                   return true;
                               });
    }

    bool tc_retained_multi_chart2d_set_panel_layout(
        tc_retained_multi_chart2d* chart,
        float panel_height,
        float panel_gap)
    {
        return chart && logged("set_panel_layout",
                               false,
                               [&]
                               {
                                   chart->value.set_panel_layout(
                                       panel_height, panel_gap);
                                   return true;
                               });
    }

    bool tc_retained_multi_chart2d_set_scroll_offset(
        tc_retained_multi_chart2d* chart, float scroll_offset)
    {
        return chart && logged("set_scroll_offset",
                               false,
                               [&]
                               {
                                   chart->value.set_scroll_offset(
                                       scroll_offset);
                                   return true;
                               });
    }

    bool tc_retained_multi_chart2d_set_shared_x(
        tc_retained_multi_chart2d* chart, double x_min, double x_max)
    {
        return chart && logged("set_shared_x",
                               false,
                               [&]
                               {
                                   chart->value.set_shared_x(x_min, x_max);
                                   return true;
                               });
    }

    bool tc_retained_multi_chart2d_set_theme(
        tc_retained_multi_chart2d* chart, const tc_chart2d_theme* theme)
    {
        return chart && theme && logged("set_theme",
                                       false,
                                       [&]
                                       {
                                           chart->value.set_theme(*theme);
                                           return true;
                                       });
    }

    tc_multi_chart_panel_handle2d tc_retained_multi_chart2d_panel_at(
        const tc_retained_multi_chart2d* chart, size_t index)
    {
        return chart ? chart->value.panel_at(index) : invalid_panel();
    }

    bool tc_retained_multi_chart2d_panel_is_valid(
        const tc_retained_multi_chart2d* chart,
        tc_multi_chart_panel_handle2d panel)
    {
        return chart && chart->value.panel_valid(panel);
    }

    tc_retained_chart2d* tc_retained_multi_chart2d_panel_chart(
        tc_retained_multi_chart2d* chart,
        tc_multi_chart_panel_handle2d panel)
    {
        return chart ? chart->value.panel_chart(panel) : nullptr;
    }
}
