#include "tcplot/retained_chart2d.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <tcbase/tc_log.hpp>
#include <termin_visual_scene/scene2d.hpp>
#include <termin_visual_scene/tc_visual_scene_item2d.h>
#include <tgfx2/font_atlas.hpp>

#include "tcplot/chart_interaction2d.hpp"
#include "tcplot/gpu_host.hpp"
#include "tcplot/plot_frame2d.hpp"
#include "tcplot/plot_layout2d.hpp"
#include "tcplot/plot_series_item2d.hpp"

namespace
{

    constexpr const char* kGroupType = "termin.visual.Group2D";
    constexpr const char* kRectType = "termin.visual.Rect2D";
    constexpr const char* kPathType = "termin.visual.Path2D";
    constexpr const char* kTextType = "termin.visual.Text2D";
    constexpr const char* kGridType = "tcplot.PlotGridItem2D";
    constexpr const char* kLineType = "tcplot.PlotLineSeriesItem2D";
    constexpr const char* kScatterType = "tcplot.PlotScatterSeriesItem2D";

    std::atomic<uint64_t> next_chart_id{1};

    tc_chart_series_handle2d invalid_series()
    {
        return {0, UINT32_MAX, 0};
    }

    bool same(tc_graphic_item_handle a, tc_graphic_item_handle b)
    {
        return a.scene_id == b.scene_id && a.index == b.index &&
               a.generation == b.generation;
    }

    bool finite_color(tc_visual_color4f color)
    {
        return std::isfinite(color.r) && std::isfinite(color.g) &&
               std::isfinite(color.b) && std::isfinite(color.a);
    }

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

    bool valid_theme(const tc_chart2d_theme& theme)
    {
        return finite_color(theme.background_color) &&
               finite_color(theme.plot_background_color) &&
               finite_color(theme.foreground_color) &&
               finite_color(theme.axis_color) &&
               std::isfinite(theme.grid_style.r) &&
               std::isfinite(theme.grid_style.g) &&
               std::isfinite(theme.grid_style.b) &&
               std::isfinite(theme.grid_style.a) &&
               std::isfinite(theme.grid_style.width_px) &&
               theme.grid_style.width_px > 0.0f &&
               std::isfinite(theme.axis_width_logical_px) &&
               theme.axis_width_logical_px > 0.0f &&
               std::isfinite(theme.font_size_logical_px) &&
               theme.font_size_logical_px > 0.0f &&
               std::isfinite(theme.title_font_size_logical_px) &&
               theme.title_font_size_logical_px > 0.0f &&
               std::isfinite(theme.tick_length_logical_px) &&
               theme.tick_length_logical_px >= 0.0f &&
               std::isfinite(theme.gap_logical_px) &&
               theme.gap_logical_px >= 0.0f &&
               std::isfinite(theme.outer_padding_logical_px) &&
               theme.outer_padding_logical_px >= 0.0f &&
               std::isfinite(theme.x_tick_spacing_logical_px) &&
               theme.x_tick_spacing_logical_px > 0.0f &&
               std::isfinite(theme.y_tick_spacing_logical_px) &&
               theme.y_tick_spacing_logical_px > 0.0f;
    }

    tc_visual_fill_paint2d fill(tc_visual_color4f color)
    {
        return {color, TC_VISUAL_FILL_RULE_NON_ZERO};
    }

    tc_visual_stroke_paint2d stroke(tc_visual_color4f color, float width)
    {
        return {color,
                width,
                TC_VISUAL_STROKE_JOIN_MITER,
                TC_VISUAL_STROKE_CAP_SQUARE,
                4.0f,
                nullptr,
                0,
                0.0f};
    }

    tc_rect2f visual_rect(tc_plot_rect2d rect)
    {
        return {rect.x, rect.y, rect.width, rect.height};
    }

    tc_bounds2f visual_bounds(tc_plot_rect2d rect)
    {
        return {rect.x, rect.y, rect.x + rect.width, rect.y + rect.height};
    }

    tc_visual_path2d_view
    path_view(const std::vector<tc_visual_path_verb2d>& verbs,
              const std::vector<tc_vec2f>& points)
    {
        return {verbs.data(), verbs.size(), points.data(), points.size()};
    }

    void append_path_line(std::vector<tc_visual_path_verb2d>& verbs,
                          std::vector<tc_vec2f>& points,
                          float x0,
                          float y0,
                          float x1,
                          float y1)
    {
        verbs.push_back(TC_VISUAL_PATH_MOVE_TO);
        verbs.push_back(TC_VISUAL_PATH_LINE_TO);
        points.push_back({x0, y0});
        points.push_back({x1, y1});
    }

    tc_chart2d_theme default_theme()
    {
        return {
            {0.08f, 0.09f, 0.11f, 1.0f},
            {0.12f, 0.13f, 0.16f, 1.0f},
            {0.88f, 0.89f, 0.92f, 1.0f},
            {0.65f, 0.67f, 0.72f, 1.0f},
            {0.35f, 0.37f, 0.42f, 0.55f, 1.0f},
            1.0f,
            12.0f,
            16.0f,
            5.0f,
            6.0f,
            10.0f,
            80.0f,
            50.0f,
        };
    }

    class RetainedChart2D
    {
    public:
        RetainedChart2D(tcplot::GpuHost& host,
                        tc_visual_scene_handle scene,
                        bool owns_scene,
                        tc_plot_rect2d viewport,
                        tc_plot_range2d range,
                        std::string font_uri,
                        float pixel_scale,
                        tc_chart2d_theme theme)
            : host_(&host), scene_(scene), owns_scene_(owns_scene),
              viewport_(viewport), range_(range),
              font_uri_(std::move(font_uri)), pixel_scale_(pixel_scale),
              theme_(theme)
        {
            if (!tc_visual_scene_is_valid(scene_))
                throw std::invalid_argument(
                    "RetainedChart2D requires a live scene");
            if (!valid_rect(viewport_) || !valid_range(range_) ||
                !std::isfinite(pixel_scale_) || pixel_scale_ <= 0.0f ||
                font_uri_.empty() || !valid_theme(theme_))
            {
                throw std::invalid_argument(
                    "RetainedChart2D configuration is invalid");
            }
            try
            {
                create_tree();
                apply_layout();
            }
            catch (...)
            {
                cleanup();
                throw;
            }
        }

        ~RetainedChart2D()
        {
            cleanup();
        }

        tc_visual_scene_handle scene() const
        {
            return scene_;
        }
        tc_plot_projection_handle2d projection() const
        {
            return projection_;
        }
        tc_plot_range2d range() const
        {
            return range_;
        }
        tc_chart2d_theme theme() const
        {
            return theme_;
        }
        tc_chart2d_layout layout() const
        {
            return {viewport_, plot_area_, pixel_scale_, layout_revision_};
        }

        tc_graphic_item_handle part(tc_chart2d_part value) const
        {
            switch (value)
            {
            case TC_CHART2D_PART_ROOT:
                return root_;
            case TC_CHART2D_PART_BACKGROUND:
                return background_;
            case TC_CHART2D_PART_PLOT_AREA:
                return plot_area_root_;
            case TC_CHART2D_PART_PLOT_BACKGROUND:
                return plot_background_;
            case TC_CHART2D_PART_GRID:
                return grid_;
            case TC_CHART2D_PART_SERIES_ROOT:
                return series_root_;
            case TC_CHART2D_PART_ANNOTATIONS_ROOT:
                return annotations_root_;
            case TC_CHART2D_PART_CHROME_ROOT:
                return chrome_root_;
            case TC_CHART2D_PART_X_AXIS_ROOT:
                return x_axis_root_;
            case TC_CHART2D_PART_Y_AXIS_ROOT:
                return y_axis_root_;
            case TC_CHART2D_PART_X_AXIS:
                return x_axis_;
            case TC_CHART2D_PART_Y_AXIS:
                return y_axis_;
            case TC_CHART2D_PART_X_TICK_LABELS_ROOT:
                return x_tick_labels_root_;
            case TC_CHART2D_PART_Y_TICK_LABELS_ROOT:
                return y_tick_labels_root_;
            case TC_CHART2D_PART_TITLE:
                return title_item_;
            case TC_CHART2D_PART_X_AXIS_LABEL:
                return x_axis_label_item_;
            case TC_CHART2D_PART_Y_AXIS_LABEL:
                return y_axis_label_item_;
            case TC_CHART2D_PART_LEGEND_ROOT:
                return legend_root_;
            case TC_CHART2D_PART_OVERLAY_ROOT:
                return overlay_root_;
            }
            return tc_graphic_item_handle_invalid();
        }

        void set_viewport(tc_plot_rect2d viewport, float pixel_scale)
        {
            if (!valid_rect(viewport) || !std::isfinite(pixel_scale) ||
                pixel_scale <= 0.0f)
                throw std::invalid_argument("chart viewport is invalid");
            viewport_ = viewport;
            pixel_scale_ = pixel_scale;
            apply_layout();
        }

        void set_range(tc_plot_range2d range)
        {
            if (!valid_range(range))
                throw std::invalid_argument("chart range is invalid");
            range_ = range;
            apply_layout();
        }

        void set_frame(tc_plot_rect2d viewport,
                       float pixel_scale,
                       tc_plot_range2d range)
        {
            if (!valid_rect(viewport) || !std::isfinite(pixel_scale) ||
                pixel_scale <= 0.0f || !valid_range(range))
                throw std::invalid_argument("chart frame is invalid");
            viewport_ = viewport;
            pixel_scale_ = pixel_scale;
            range_ = range;
            apply_layout();
        }

        void fit(double padding_fraction, bool fit_x, bool fit_y)
        {
            if (!std::isfinite(padding_fraction) || padding_fraction < 0.0)
                throw std::invalid_argument("chart fit padding is invalid");
            const std::optional<tcplot::PlotRange2D> bounds = series_bounds();
            const auto fitted =
                tcplot::fit_optional_plot_range2d(bounds, padding_fraction);
            if (!fitted)
                throw std::runtime_error("failed to fit chart series bounds");
            const tc_plot_range2d next = {
                fit_x ? fitted->x_min() : range_.x_min,
                fit_x ? fitted->x_max() : range_.x_max,
                fit_y ? fitted->y_min() : range_.y_min,
                fit_y ? fitted->y_max() : range_.y_max,
            };
            set_range(next);
        }

        bool pointer_down(float x, float y, int button)
        {
            return interaction_.pointer_down(frame(), x, y, button);
        }

        bool pointer_move(float x, float y)
        {
            const auto next = interaction_.pointer_move(x, y);
            if (!next)
                return false;
            set_range(c_range(*next));
            return true;
        }

        bool pointer_up(int button)
        {
            return interaction_.pointer_up(button);
        }

        bool wheel(float x, float y, float steps, bool x_only)
        {
            const auto next = interaction_.wheel(frame(), x, y, steps, x_only);
            if (!next)
                return false;
            set_range(c_range(*next));
            return true;
        }

        void cancel_interaction()
        {
            interaction_.cancel();
        }

        void set_theme(tc_chart2d_theme theme)
        {
            if (!valid_theme(theme))
                throw std::invalid_argument("chart theme is invalid");
            theme_ = theme;
            apply_layout();
        }

        void set_title(std::string value)
        {
            title_ = std::move(value);
            apply_layout();
        }
        void set_x_label(std::string value)
        {
            x_label_ = std::move(value);
            apply_layout();
        }
        void set_y_label(std::string value)
        {
            y_label_ = std::move(value);
            apply_layout();
        }

        bool replace_part(tc_chart2d_part part_id,
                          tc_graphic_item_handle replacement)
        {
            PartSlot slot = replaceable_part(part_id);
            if (!slot.handle || !slot.parent || slot.type == nullptr ||
                tc_graphic_item_handle_is_invalid(replacement) ||
                !tc_visual_scene_item_is_type(scene_, replacement, slot.type))
                return false;
            if (same(*slot.handle, replacement))
                return true;
            const size_t index =
                tc_visual_scene_item_child_count(scene_, *slot.parent);
            if (!tc_visual_scene_item_set_parent(
                    scene_, replacement, *slot.parent, index) ||
                !tc_visual_scene_item_set_z_order(
                    scene_, replacement, slot.z_order))
                return false;
            const tc_graphic_item_handle previous = *slot.handle;
            *slot.handle = replacement;
            if (!tc_graphic_item_handle_is_invalid(previous))
                tc_visual_scene_destroy_item(scene_, previous);
            apply_layout();
            return true;
        }

        bool remove_part(tc_chart2d_part part_id)
        {
            PartSlot slot = replaceable_part(part_id);
            if (!slot.handle || tc_graphic_item_handle_is_invalid(*slot.handle))
                return false;
            const tc_graphic_item_handle previous = *slot.handle;
            *slot.handle = tc_graphic_item_handle_invalid();
            return tc_visual_scene_destroy_item(scene_, previous);
        }

        tc_graphic_item_handle add_line(const double* x,
                                        const double* y,
                                        const double* scalar,
                                        size_t count,
                                        tc_plot_line_style_state2d style)
        {
            const tc_chart_series_handle2d series = add_named_line(
                "", false, x, y, scalar, count, style);
            const SeriesSlot* slot = resolve_series(series);
            return slot ? slot->item : tc_graphic_item_handle_invalid();
        }

        tc_graphic_item_handle add_scatter(const double* x,
                                           const double* y,
                                           size_t count,
                                           tc_plot_scatter_style_state2d style)
        {
            const tc_chart_series_handle2d series = add_named_scatter(
                "", false, x, y, count, style);
            const SeriesSlot* slot = resolve_series(series);
            return slot ? slot->item : tc_graphic_item_handle_invalid();
        }

        bool remove_series(tc_graphic_item_handle item)
        {
            for (SeriesSlot& slot : series_)
            {
                if (slot.alive && same(slot.item, item))
                    return remove_semantic_series(series_handle(slot));
            }
            if ((!tc_visual_scene_item_is_type(scene_, item, kLineType) &&
                 !tc_visual_scene_item_is_type(scene_, item, kScatterType)) ||
                !same(tc_visual_scene_item_parent(scene_, item), series_root_))
                return false;
            return tc_visual_scene_destroy_item(scene_, item);
        }

        tc_chart_series_handle2d add_named_line(
            std::string name,
            bool show_in_legend,
            const double* x,
            const double* y,
            const double* scalar,
            size_t count,
            tc_plot_line_style_state2d style)
        {
            tc_graphic_item_handle item = adopt_series(
                tc_plot_line_series_item2d_create(
                    scene_, projection_, x, y, scalar, count, style));
            if (tc_graphic_item_handle_is_invalid(item))
                throw std::runtime_error("failed to create chart line series");
            SeriesSlot* allocated = nullptr;
            try
            {
                SeriesSlot& slot = allocate_series(TC_CHART_SERIES_LINE_2D);
                allocated = &slot;
                slot.item = item;
                slot.name = std::move(name);
                slot.show_in_legend = show_in_legend;
                create_legend_items(slot);
                update_legend();
                return series_handle(slot);
            }
            catch (...)
            {
                if (allocated)
                    rollback_series_allocation(*allocated);
                tc_visual_scene_destroy_item(scene_, item);
                throw;
            }
        }

        tc_chart_series_handle2d add_named_scatter(
            std::string name,
            bool show_in_legend,
            const double* x,
            const double* y,
            size_t count,
            tc_plot_scatter_style_state2d style)
        {
            tc_graphic_item_handle item = adopt_series(
                tc_plot_scatter_series_item2d_create(
                    scene_, projection_, x, y, count, style));
            if (tc_graphic_item_handle_is_invalid(item))
                throw std::runtime_error(
                    "failed to create chart scatter series");
            SeriesSlot* allocated = nullptr;
            try
            {
                SeriesSlot& slot =
                    allocate_series(TC_CHART_SERIES_SCATTER_2D);
                allocated = &slot;
                slot.item = item;
                slot.name = std::move(name);
                slot.show_in_legend = show_in_legend;
                create_legend_items(slot);
                update_legend();
                return series_handle(slot);
            }
            catch (...)
            {
                if (allocated)
                    rollback_series_allocation(*allocated);
                tc_visual_scene_destroy_item(scene_, item);
                throw;
            }
        }

        bool series_valid(tc_chart_series_handle2d series) const
        {
            return resolve_series(series) != nullptr;
        }

        bool series_snapshot(tc_chart_series_handle2d series,
                             tc_chart_series_snapshot2d& snapshot) const
        {
            const SeriesSlot* slot = resolve_series(series);
            if (!slot)
                return false;
            const auto bounds = series_item_bounds(slot->item);
            bool visible = false;
            if (!tc_visual_scene_item_get_visible(
                    scene_, slot->item, &visible))
                return false;
            snapshot = {
                slot->kind,
                slot->item,
                visible,
                slot->show_in_legend,
                bounds.has_value(),
                bounds ? c_range(*bounds) : tc_plot_range2d{},
            };
            return true;
        }

        const std::string* series_name(tc_chart_series_handle2d series) const
        {
            const SeriesSlot* slot = resolve_series(series);
            return slot ? &slot->name : nullptr;
        }

        bool set_series_name(tc_chart_series_handle2d series,
                             std::string name)
        {
            SeriesSlot* slot = resolve_series(series);
            if (!slot)
                return false;
            slot->name = std::move(name);
            update_legend();
            return true;
        }

        bool set_series_visible(tc_chart_series_handle2d series, bool visible)
        {
            SeriesSlot* slot = resolve_series(series);
            if (!slot || !tc_visual_scene_item_set_visible(
                             scene_, slot->item, visible))
                return false;
            update_legend();
            return true;
        }

        bool set_series_legend_visible(tc_chart_series_handle2d series,
                                       bool show)
        {
            SeriesSlot* slot = resolve_series(series);
            if (!slot)
                return false;
            slot->show_in_legend = show;
            update_legend();
            return true;
        }

        bool set_line_series_style(tc_chart_series_handle2d series,
                                   tc_plot_line_style_state2d style)
        {
            SeriesSlot* slot = resolve_series(series);
            if (!slot || slot->kind != TC_CHART_SERIES_LINE_2D ||
                !tc_plot_line_series_item2d_set_style(
                    scene_, slot->item, style))
                return false;
            update_legend();
            return true;
        }

        bool set_scatter_series_style(tc_chart_series_handle2d series,
                                      tc_plot_scatter_style_state2d style)
        {
            SeriesSlot* slot = resolve_series(series);
            if (!slot || slot->kind != TC_CHART_SERIES_SCATTER_2D ||
                !tc_plot_scatter_series_item2d_set_style(
                    scene_, slot->item, style))
                return false;
            update_legend();
            return true;
        }

        bool remove_semantic_series(tc_chart_series_handle2d series)
        {
            SeriesSlot* slot = resolve_series_slot(series);
            if (!slot)
                return false;
            const uint32_t index = slot->index;
            if (!tc_graphic_item_handle_is_invalid(slot->legend_group))
                tc_visual_scene_destroy_item(scene_, slot->legend_group);
            if (!tc_graphic_item_handle_is_invalid(slot->item))
                tc_visual_scene_destroy_item(scene_, slot->item);
            slot->alive = false;
            slot->item = tc_graphic_item_handle_invalid();
            slot->legend_group = tc_graphic_item_handle_invalid();
            slot->legend_swatch = tc_graphic_item_handle_invalid();
            slot->legend_label = tc_graphic_item_handle_invalid();
            slot->name.clear();
            ++slot->generation;
            if (slot->generation == 0)
                ++slot->generation;
            free_series_.push_back(index);
            update_legend();
            return true;
        }

    private:
        tcplot::PlotFrame2D frame() const
        {
            return {
                {viewport_.x, viewport_.y, viewport_.width, viewport_.height},
                {plot_area_.x,
                 plot_area_.y,
                 plot_area_.width,
                 plot_area_.height},
                {range_.x_min, range_.x_max, range_.y_min, range_.y_max},
                {plot_area_.x,
                 plot_area_.y,
                 plot_area_.width,
                 plot_area_.height},
                pixel_scale_,
            };
        }

        static tc_plot_range2d c_range(const tcplot::PlotRange2D& value)
        {
            return {value.x_min(), value.x_max(), value.y_min(), value.y_max()};
        }

        struct PartSlot
        {
            tc_graphic_item_handle* handle = nullptr;
            tc_graphic_item_handle* parent = nullptr;
            const char* type = nullptr;
            int64_t z_order = 0;
        };

        struct SeriesSlot
        {
            uint32_t index = 0;
            uint32_t generation = 1;
            bool alive = false;
            tc_chart_series_kind2d kind = TC_CHART_SERIES_INVALID_2D;
            tc_graphic_item_handle item = tc_graphic_item_handle_invalid();
            std::string name;
            bool show_in_legend = false;
            tc_graphic_item_handle legend_group =
                tc_graphic_item_handle_invalid();
            tc_graphic_item_handle legend_swatch =
                tc_graphic_item_handle_invalid();
            tc_graphic_item_handle legend_label =
                tc_graphic_item_handle_invalid();
        };

        SeriesSlot& allocate_series(tc_chart_series_kind2d kind)
        {
            uint32_t index = 0;
            if (free_series_.empty())
            {
                index = static_cast<uint32_t>(series_.size());
                series_.push_back({});
                series_.back().index = index;
            }
            else
            {
                index = free_series_.back();
                free_series_.pop_back();
            }
            SeriesSlot& slot = series_[index];
            slot.alive = true;
            slot.kind = kind;
            slot.show_in_legend = false;
            return slot;
        }

        void rollback_series_allocation(SeriesSlot& slot)
        {
            if (!tc_graphic_item_handle_is_invalid(slot.legend_group))
                tc_visual_scene_destroy_item(scene_, slot.legend_group);
            slot.alive = false;
            slot.item = tc_graphic_item_handle_invalid();
            slot.legend_group = tc_graphic_item_handle_invalid();
            slot.legend_swatch = tc_graphic_item_handle_invalid();
            slot.legend_label = tc_graphic_item_handle_invalid();
            slot.name.clear();
            ++slot.generation;
            if (slot.generation == 0)
                ++slot.generation;
            free_series_.push_back(slot.index);
        }

        tc_chart_series_handle2d series_handle(const SeriesSlot& slot) const
        {
            return {chart_id_, slot.index, slot.generation};
        }

        SeriesSlot* resolve_series_slot(tc_chart_series_handle2d series)
        {
            if (series.chart_id != chart_id_ ||
                series.index >= series_.size())
                return nullptr;
            SeriesSlot& slot = series_[series.index];
            if (!slot.alive || slot.generation != series.generation)
                return nullptr;
            return &slot;
        }

        SeriesSlot* resolve_series(tc_chart_series_handle2d series)
        {
            SeriesSlot* slot = resolve_series_slot(series);
            if (!slot)
                return nullptr;
            const char* expected = slot->kind == TC_CHART_SERIES_LINE_2D
                                       ? kLineType
                                       : slot->kind ==
                                                     TC_CHART_SERIES_SCATTER_2D
                                             ? kScatterType
                                             : nullptr;
            return expected && tc_visual_scene_item_is_type(
                                   scene_, slot->item, expected)
                       ? slot
                       : nullptr;
        }

        const SeriesSlot* resolve_series(
            tc_chart_series_handle2d series) const
        {
            return const_cast<RetainedChart2D*>(this)->resolve_series(series);
        }

        void require(tc_graphic_item_handle handle, const char* operation)
        {
            if (tc_graphic_item_handle_is_invalid(handle))
                throw std::runtime_error(operation);
        }

        void set_parent(tc_graphic_item_handle child,
                        tc_graphic_item_handle parent)
        {
            const size_t index =
                tc_visual_scene_item_child_count(scene_, parent);
            if (!tc_visual_scene_item_set_parent(scene_, child, parent, index))
                throw std::runtime_error("failed to adopt chart item");
        }

        tc_graphic_item_handle create_group(tc_graphic_item_handle parent)
        {
            tc_graphic_item_handle result =
                tc_visual_group_item2d_create(scene_, parent);
            require(result, "failed to create chart group");
            return result;
        }

        tc_graphic_item_handle create_rect(tc_graphic_item_handle parent,
                                           tc_visual_color4f color)
        {
            tc_graphic_item_handle result = tc_visual_rect_item2d_create(
                scene_, parent, visual_rect(viewport_), fill(color), nullptr);
            require(result, "failed to create chart rectangle");
            return result;
        }

        tc_graphic_item_handle create_path(tc_graphic_item_handle parent)
        {
            const std::vector<tc_visual_path_verb2d> verbs = {
                TC_VISUAL_PATH_MOVE_TO, TC_VISUAL_PATH_LINE_TO};
            const std::vector<tc_vec2f> points = {{0, 0}, {1, 0}};
            tc_visual_stroke_paint2d paint = stroke(theme_.axis_color, 1.0f);
            tc_graphic_item_handle result = tc_visual_path_item2d_create(
                scene_, parent, path_view(verbs, points), nullptr, &paint);
            require(result, "failed to create chart path");
            return result;
        }

        tc_graphic_item_handle create_text(tc_graphic_item_handle parent,
                                           tc_visual_text_anchor2d anchor)
        {
            const tc_visual_text_desc2d desc = {" ",
                                                font_uri_.c_str(),
                                                {0, 0},
                                                theme_.font_size_logical_px,
                                                theme_.foreground_color,
                                                anchor,
                                                visual_bounds(viewport_)};
            tc_graphic_item_handle result =
                tc_visual_text_item2d_create(scene_, parent, &desc);
            require(result, "failed to create chart text");
            return result;
        }

        void create_legend_items(SeriesSlot& slot)
        {
            slot.legend_group = create_group(legend_root_);
            slot.legend_swatch = create_path(slot.legend_group);
            slot.legend_label = create_text(
                slot.legend_group, TC_VISUAL_TEXT_ANCHOR_LEFT);
            tc_visual_scene_item_set_z_order(
                scene_, slot.legend_group, 1);
        }

        tc_visual_stroke_paint2d legend_stroke(const SeriesSlot& slot) const
        {
            tc_visual_color4f color = theme_.foreground_color;
            float width = 2.0f * pixel_scale_;
            if (slot.kind == TC_CHART_SERIES_LINE_2D)
            {
                tc_plot_series_snapshot2d snapshot{};
                tc_plot_line_style_state2d style{};
                if (tc_plot_line_series_item2d_snapshot(
                        scene_, slot.item, &snapshot, &style))
                {
                    color = {style.color.r,
                             style.color.g,
                             style.color.b,
                             style.color.a};
                    width = std::max(1.0f, style.thickness_px);
                }
            }
            else if (slot.kind == TC_CHART_SERIES_SCATTER_2D)
            {
                tc_plot_series_snapshot2d snapshot{};
                tc_plot_scatter_style_state2d style{};
                if (tc_plot_scatter_series_item2d_snapshot(
                        scene_, slot.item, &snapshot, &style))
                {
                    color = {style.color.r,
                             style.color.g,
                             style.color.b,
                             style.color.a};
                    width = std::max(2.0f, style.diameter_px);
                }
            }
            bool visible = true;
            tc_visual_scene_item_get_visible(scene_, slot.item, &visible);
            if (!visible)
                color.a *= 0.35f;
            return stroke(color, width);
        }

        void update_legend()
        {
            if (tc_graphic_item_handle_is_invalid(legend_root_))
                return;
            const float scale = pixel_scale_;
            const float gap = theme_.gap_logical_px * scale;
            const float padding = std::max(4.0f * scale, gap);
            const float font_size = theme_.font_size_logical_px * scale;
            const auto metrics = tcplot::measure_plot_text2d(
                host_->font(), "Mg", theme_.font_size_logical_px, scale);
            if (!metrics)
                throw std::runtime_error("failed to measure chart legend");

            float widest_label = 0.0f;
            size_t visible_count = 0;
            for (SeriesSlot& slot : series_)
            {
                const bool participates = slot.alive &&
                                          slot.show_in_legend &&
                                          !slot.name.empty();
                if (!tc_graphic_item_handle_is_invalid(slot.legend_group) &&
                    !tc_visual_scene_item_set_visible(
                        scene_, slot.legend_group, participates))
                    throw std::runtime_error(
                        "failed to update legend entry visibility");
                if (!participates)
                    continue;
                const auto measured = tcplot::measure_plot_text2d(
                    host_->font(), slot.name, theme_.font_size_logical_px, scale);
                if (!measured)
                    throw std::runtime_error("failed to measure legend label");
                widest_label = std::max(widest_label, measured->width);
                ++visible_count;
            }

            const bool visible = visible_count != 0;
            if (!tc_visual_scene_item_set_visible(
                    scene_, legend_root_, visible) ||
                !tc_visual_scene_item_set_visible(
                    scene_, legend_background_, visible))
                throw std::runtime_error("failed to update legend visibility");
            if (!visible)
                return;

            const float swatch_width = 24.0f * scale;
            const float row_height = std::max(
                metrics->line_height, 12.0f * scale) + gap;
            const float width = padding * 2.0f + swatch_width + gap +
                                widest_label;
            const float height = padding * 2.0f +
                                 row_height * visible_count - gap;
            const float left = plot_area_.x + plot_area_.width - width - gap;
            const float top = plot_area_.y + gap;
            tc_visual_color4f background = theme_.plot_background_color;
            background.a = std::max(background.a, 0.9f);
            set_rect(legend_background_, {left, top, width, height}, background);

            size_t row = 0;
            for (SeriesSlot& slot : series_)
            {
                if (!slot.alive || !slot.show_in_legend || slot.name.empty())
                    continue;
                const float center_y = top + padding +
                                       row * row_height +
                                       metrics->line_height * 0.5f;
                const std::vector<tc_visual_path_verb2d> verbs = {
                    TC_VISUAL_PATH_MOVE_TO, TC_VISUAL_PATH_LINE_TO};
                const std::vector<tc_vec2f> points = {
                    {left + padding, center_y},
                    {left + padding + swatch_width, center_y}};
                tc_visual_stroke_paint2d paint = legend_stroke(slot);
                if (!tc_visual_path_item2d_set(scene_,
                                               slot.legend_swatch,
                                               path_view(verbs, points),
                                               nullptr,
                                               &paint))
                    throw std::runtime_error("failed to update legend swatch");
                set_text(slot.legend_label,
                         slot.name,
                         {left + padding + swatch_width + gap,
                          center_y + metrics->ascent * 0.5f},
                         font_size,
                         TC_VISUAL_TEXT_ANCHOR_LEFT);
                ++row;
            }
        }

        void create_tree()
        {
            root_ = create_group(tc_graphic_item_handle_invalid());
            background_ = create_rect(root_, theme_.background_color);
            plot_area_root_ = create_group(root_);
            plot_background_ =
                create_rect(plot_area_root_, theme_.plot_background_color);

            const tc_plot_projection_desc2d provisional = {
                viewport_, viewport_, range_, viewport_, pixel_scale_};
            projection_ = tc_plot_projection2d_create(scene_, &provisional);
            if (tc_plot_projection_handle2d_is_invalid(projection_))
                throw std::runtime_error("failed to create chart projection");
            grid_ = tc_plot_grid_item2d_create(
                scene_, projection_, nullptr, 0, nullptr, 0, theme_.grid_style);
            require(grid_, "failed to create chart grid");
            set_parent(grid_, plot_area_root_);

            series_root_ = create_group(plot_area_root_);
            annotations_root_ = create_group(plot_area_root_);
            chrome_root_ = create_group(root_);
            x_axis_root_ = create_group(chrome_root_);
            y_axis_root_ = create_group(chrome_root_);
            x_axis_ = create_path(x_axis_root_);
            y_axis_ = create_path(y_axis_root_);
            x_tick_labels_root_ = create_group(x_axis_root_);
            y_tick_labels_root_ = create_group(y_axis_root_);
            title_item_ =
                create_text(chrome_root_, TC_VISUAL_TEXT_ANCHOR_CENTER);
            x_axis_label_item_ =
                create_text(chrome_root_, TC_VISUAL_TEXT_ANCHOR_CENTER);
            y_axis_label_item_ =
                create_text(chrome_root_, TC_VISUAL_TEXT_ANCHOR_LEFT);
            legend_root_ = create_group(root_);
            legend_background_ =
                create_rect(legend_root_, theme_.plot_background_color);
            overlay_root_ = create_group(root_);

            tc_visual_scene_item_set_z_order(scene_, background_, -100);
            tc_visual_scene_item_set_z_order(scene_, plot_area_root_, 0);
            tc_visual_scene_item_set_z_order(scene_, plot_background_, 0);
            tc_visual_scene_item_set_z_order(scene_, grid_, 10);
            tc_visual_scene_item_set_z_order(scene_, series_root_, 20);
            tc_visual_scene_item_set_z_order(scene_, annotations_root_, 30);
            tc_visual_scene_item_set_z_order(scene_, chrome_root_, 40);
            tc_visual_scene_item_set_z_order(scene_, legend_root_, 50);
            tc_visual_scene_item_set_z_order(scene_, legend_background_, 0);
            tc_visual_scene_item_set_z_order(scene_, overlay_root_, 60);
        }

        void apply_layout()
        {
            if (!tc_visual_scene_is_valid(scene_))
                throw std::runtime_error("chart scene is stale");
            const float scale = pixel_scale_;
            const float padding = theme_.outer_padding_logical_px * scale;
            const float gap = theme_.gap_logical_px * scale;
            const float tick_length = theme_.tick_length_logical_px * scale;
            const float tick_font_px = theme_.font_size_logical_px * scale;
            const float title_font_px =
                theme_.title_font_size_logical_px * scale;
            const float provisional_width =
                std::max(1.0f, viewport_.width - padding * 2.0f);
            const float provisional_height =
                std::max(1.0f, viewport_.height - padding * 2.0f);

            const tcplot::PlotFrame2D provisional(
                {viewport_.x, viewport_.y, viewport_.width, viewport_.height},
                {viewport_.x + padding,
                 viewport_.y + padding,
                 provisional_width,
                 provisional_height},
                {range_.x_min, range_.x_max, range_.y_min, range_.y_max},
                {viewport_.x, viewport_.y, viewport_.width, viewport_.height},
                scale);
            const auto ticks =
                tcplot::make_plot_ticks2d(provisional,
                                          theme_.x_tick_spacing_logical_px,
                                          theme_.y_tick_spacing_logical_px);
            const auto tick_metrics = tcplot::measure_plot_text2d(
                host_->font(), "Mg", theme_.font_size_logical_px, scale);
            if (!ticks || !tick_metrics)
                throw std::runtime_error("failed to measure chart layout");

            float widest_y = 0.0f;
            for (const std::string& label : ticks->y.labels)
            {
                const auto measured = tcplot::measure_plot_text2d(
                    host_->font(), label, theme_.font_size_logical_px, scale);
                if (!measured)
                    throw std::runtime_error("failed to measure Y tick label");
                widest_y = std::max(widest_y, measured->width);
            }
            float title_height = 0.0f;
            if (!title_.empty())
            {
                const auto measured = tcplot::measure_plot_text2d(
                    host_->font(),
                    title_,
                    theme_.title_font_size_logical_px,
                    scale);
                if (!measured)
                    throw std::runtime_error("failed to measure chart title");
                title_height = measured->line_height;
            }
            const float x_label_height =
                x_label_.empty() ? 0.0f : tick_metrics->line_height + gap;
            float y_label_width = 0.0f;
            if (!y_label_.empty())
            {
                const auto measured =
                    tcplot::measure_plot_text2d(host_->font(),
                                                y_label_,
                                                theme_.font_size_logical_px,
                                                scale);
                if (!measured)
                    throw std::runtime_error("failed to measure Y axis label");
                y_label_width = measured->width + gap;
            }

            const float left = viewport_.x + padding + y_label_width +
                               widest_y + tick_length + gap * 2.0f;
            const float top = viewport_.y + padding + title_height +
                              (title_height > 0.0f ? gap : 0.0f);
            const float right = padding;
            const float bottom = padding + tick_length + gap +
                                 tick_metrics->line_height + x_label_height;
            plot_area_ = {
                left,
                top,
                std::max(1.0f, viewport_.x + viewport_.width - left - right),
                std::max(1.0f, viewport_.y + viewport_.height - top - bottom)};

            const tc_plot_projection_desc2d projection_desc = {
                viewport_, plot_area_, range_, plot_area_, scale};
            if (!tc_plot_projection2d_update(projection_, &projection_desc))
                throw std::runtime_error("failed to update chart projection");
            if (!tc_visual_scene_item_set_clip_rect(
                    scene_, root_, visual_rect(viewport_)) ||
                !tc_visual_scene_item_set_clip_rect(
                    scene_, plot_area_root_, visual_rect(plot_area_)))
                throw std::runtime_error("failed to update chart clipping");
            set_rect(background_, viewport_, theme_.background_color);
            set_rect(
                plot_background_, plot_area_, theme_.plot_background_color);

            const tcplot::PlotFrame2D exact_frame(
                {viewport_.x, viewport_.y, viewport_.width, viewport_.height},
                {plot_area_.x,
                 plot_area_.y,
                 plot_area_.width,
                 plot_area_.height},
                {range_.x_min, range_.x_max, range_.y_min, range_.y_max},
                {plot_area_.x,
                 plot_area_.y,
                 plot_area_.width,
                 plot_area_.height},
                scale);
            const auto exact_ticks =
                tcplot::make_plot_ticks2d(exact_frame,
                                          theme_.x_tick_spacing_logical_px,
                                          theme_.y_tick_spacing_logical_px);
            if (!exact_ticks)
                throw std::runtime_error(
                    "failed to generate exact chart ticks");
            if (!tc_graphic_item_handle_is_invalid(grid_))
            {
                if (!tc_plot_grid_item2d_set_projection(
                        scene_, grid_, projection_) ||
                    !tc_plot_grid_item2d_set_style(
                        scene_, grid_, theme_.grid_style) ||
                    !tc_plot_grid_item2d_set_ticks(
                        scene_,
                        grid_,
                        exact_ticks->x.values.data(),
                        exact_ticks->x.values.size(),
                        exact_ticks->y.values.data(),
                        exact_ticks->y.values.size()))
                    throw std::runtime_error("failed to update chart grid");
            }
            set_axes(*exact_ticks, tick_length, exact_frame);
            update_tick_labels(
                *exact_ticks, tick_font_px, *tick_metrics, exact_frame);
            set_text(title_item_,
                     title_,
                     {viewport_.x + viewport_.width * 0.5f,
                      viewport_.y + padding + title_font_px},
                     title_font_px,
                     TC_VISUAL_TEXT_ANCHOR_CENTER);
            set_text(x_axis_label_item_,
                     x_label_,
                     {plot_area_.x + plot_area_.width * 0.5f,
                      viewport_.y + viewport_.height - padding},
                     tick_font_px,
                     TC_VISUAL_TEXT_ANCHOR_CENTER);
            set_text(y_axis_label_item_,
                     y_label_,
                     {viewport_.x + padding,
                      plot_area_.y + plot_area_.height * 0.5f},
                     tick_font_px,
                     TC_VISUAL_TEXT_ANCHOR_LEFT);
            update_legend();
            ++layout_revision_;
        }

        void set_rect(tc_graphic_item_handle item,
                      tc_plot_rect2d rect,
                      tc_visual_color4f color)
        {
            if (tc_graphic_item_handle_is_invalid(item))
                return;
            if (!tc_visual_rect_item2d_set(
                    scene_, item, visual_rect(rect), fill(color), nullptr))
                throw std::runtime_error("failed to update chart rectangle");
        }

        void set_text(tc_graphic_item_handle item,
                      const std::string& text,
                      tc_vec2f origin,
                      float size,
                      tc_visual_text_anchor2d anchor)
        {
            if (tc_graphic_item_handle_is_invalid(item))
                return;
            const bool visible = !text.empty();
            const tc_visual_text_desc2d desc = {visible ? text.c_str() : " ",
                                                font_uri_.c_str(),
                                                origin,
                                                size,
                                                theme_.foreground_color,
                                                anchor,
                                                visual_bounds(viewport_)};
            if (!tc_visual_text_item2d_set(scene_, item, &desc) ||
                !tc_visual_scene_item_set_visible(scene_, item, visible))
                throw std::runtime_error("failed to update chart text");
        }

        void set_axes(const tcplot::PlotTicks2D& ticks,
                      float tick_length,
                      const tcplot::PlotFrame2D& frame)
        {
            tc_visual_stroke_paint2d paint = stroke(
                theme_.axis_color, theme_.axis_width_logical_px * pixel_scale_);
            if (!tc_graphic_item_handle_is_invalid(x_axis_))
            {
                std::vector<tc_visual_path_verb2d> verbs;
                std::vector<tc_vec2f> points;
                append_path_line(verbs,
                                 points,
                                 plot_area_.x,
                                 plot_area_.y + plot_area_.height,
                                 plot_area_.x + plot_area_.width,
                                 plot_area_.y + plot_area_.height);
                for (double value : ticks.x.values)
                {
                    const auto p = frame.data_to_pixel(value, range_.y_min);
                    append_path_line(verbs,
                                     points,
                                     p.x,
                                     plot_area_.y + plot_area_.height,
                                     p.x,
                                     plot_area_.y + plot_area_.height +
                                         tick_length);
                }
                if (!tc_visual_path_item2d_set(scene_,
                                               x_axis_,
                                               path_view(verbs, points),
                                               nullptr,
                                               &paint))
                    throw std::runtime_error("failed to update X axis");
            }
            if (!tc_graphic_item_handle_is_invalid(y_axis_))
            {
                std::vector<tc_visual_path_verb2d> verbs;
                std::vector<tc_vec2f> points;
                append_path_line(verbs,
                                 points,
                                 plot_area_.x,
                                 plot_area_.y,
                                 plot_area_.x,
                                 plot_area_.y + plot_area_.height);
                for (double value : ticks.y.values)
                {
                    const auto p = frame.data_to_pixel(range_.x_min, value);
                    append_path_line(verbs,
                                     points,
                                     plot_area_.x - tick_length,
                                     p.y,
                                     plot_area_.x,
                                     p.y);
                }
                if (!tc_visual_path_item2d_set(scene_,
                                               y_axis_,
                                               path_view(verbs, points),
                                               nullptr,
                                               &paint))
                    throw std::runtime_error("failed to update Y axis");
            }
        }

        void ensure_tick_pool(std::vector<tc_graphic_item_handle>& pool,
                              size_t count,
                              tc_graphic_item_handle parent,
                              tc_visual_text_anchor2d anchor)
        {
            while (pool.size() < count)
                pool.push_back(create_text(parent, anchor));
            for (size_t index = 0; index < pool.size(); ++index)
                tc_visual_scene_item_set_visible(
                    scene_, pool[index], index < count);
        }

        void update_tick_labels(const tcplot::PlotTicks2D& ticks,
                                float font_size,
                                const tcplot::PlotTextMetrics2D& metrics,
                                const tcplot::PlotFrame2D& frame)
        {
            ensure_tick_pool(x_tick_labels_,
                             ticks.x.values.size(),
                             x_tick_labels_root_,
                             TC_VISUAL_TEXT_ANCHOR_CENTER);
            ensure_tick_pool(y_tick_labels_,
                             ticks.y.values.size(),
                             y_tick_labels_root_,
                             TC_VISUAL_TEXT_ANCHOR_RIGHT);
            const float gap = theme_.gap_logical_px * pixel_scale_;
            const float tick_length =
                theme_.tick_length_logical_px * pixel_scale_;
            for (size_t index = 0; index < ticks.x.values.size(); ++index)
            {
                const auto p =
                    frame.data_to_pixel(ticks.x.values[index], range_.y_min);
                set_text(x_tick_labels_[index],
                         ticks.x.labels[index],
                         {p.x,
                          plot_area_.y + plot_area_.height + tick_length + gap +
                              metrics.ascent},
                         font_size,
                         TC_VISUAL_TEXT_ANCHOR_CENTER);
            }
            for (size_t index = 0; index < ticks.y.values.size(); ++index)
            {
                const auto p =
                    frame.data_to_pixel(range_.x_min, ticks.y.values[index]);
                set_text(y_tick_labels_[index],
                         ticks.y.labels[index],
                         {plot_area_.x - tick_length - gap,
                          p.y + metrics.ascent * 0.5f},
                         font_size,
                         TC_VISUAL_TEXT_ANCHOR_RIGHT);
            }
        }

        PartSlot replaceable_part(tc_chart2d_part part_id)
        {
            switch (part_id)
            {
            case TC_CHART2D_PART_BACKGROUND:
                return {&background_, &root_, kRectType, -100};
            case TC_CHART2D_PART_PLOT_BACKGROUND:
                return {&plot_background_, &plot_area_root_, kRectType, 0};
            case TC_CHART2D_PART_GRID:
                return {&grid_, &plot_area_root_, kGridType, 10};
            case TC_CHART2D_PART_X_AXIS:
                return {&x_axis_, &x_axis_root_, kPathType, 0};
            case TC_CHART2D_PART_Y_AXIS:
                return {&y_axis_, &y_axis_root_, kPathType, 0};
            case TC_CHART2D_PART_TITLE:
                return {&title_item_, &chrome_root_, kTextType, 30};
            case TC_CHART2D_PART_X_AXIS_LABEL:
                return {&x_axis_label_item_, &chrome_root_, kTextType, 30};
            case TC_CHART2D_PART_Y_AXIS_LABEL:
                return {&y_axis_label_item_, &chrome_root_, kTextType, 30};
            default:
                return {};
            }
        }

        tc_graphic_item_handle adopt_series(tc_graphic_item_handle item)
        {
            if (tc_graphic_item_handle_is_invalid(item))
                return item;
            const size_t index =
                tc_visual_scene_item_child_count(scene_, series_root_);
            if (!tc_visual_scene_item_set_parent(
                    scene_, item, series_root_, index))
            {
                tc_visual_scene_destroy_item(scene_, item);
                return tc_graphic_item_handle_invalid();
            }
            return item;
        }

        std::optional<tcplot::PlotRange2D>
        series_item_bounds(tc_graphic_item_handle handle) const
        {
            double x_min = std::numeric_limits<double>::infinity();
            double x_max = -std::numeric_limits<double>::infinity();
            double y_min = std::numeric_limits<double>::infinity();
            double y_max = -std::numeric_limits<double>::infinity();
            bool found = false;
            termin::visual::TcVisualScene scene{scene_};
            std::span<const double> x;
            std::span<const double> y;
            if (const auto* line =
                    tcplot::resolve_plot_line_series_item2d(scene, handle))
            {
                x = line->x();
                y = line->y();
            }
            else if (const auto* scatter =
                         tcplot::resolve_plot_scatter_series_item2d(scene,
                                                                    handle))
            {
                x = scatter->x();
                y = scatter->y();
            }
            else
                return std::nullopt;
            const size_t point_count = std::min(x.size(), y.size());
            for (size_t point = 0; point < point_count; ++point)
            {
                if (!std::isfinite(x[point]) || !std::isfinite(y[point]))
                    continue;
                x_min = std::min(x_min, x[point]);
                x_max = std::max(x_max, x[point]);
                y_min = std::min(y_min, y[point]);
                y_max = std::max(y_max, y[point]);
                found = true;
            }
            if (!found)
                return std::nullopt;
            return tcplot::PlotRange2D{x_min, x_max, y_min, y_max};
        }

        std::optional<tcplot::PlotRange2D> series_bounds() const
        {
            double x_min = std::numeric_limits<double>::infinity();
            double x_max = -std::numeric_limits<double>::infinity();
            double y_min = std::numeric_limits<double>::infinity();
            double y_max = -std::numeric_limits<double>::infinity();
            bool found = false;
            termin::visual::TcVisualScene scene{scene_};
            for (const SeriesSlot& slot : series_)
            {
                const tc_graphic_item* item = scene.resolve(slot.item);
                if (!slot.alive || item == nullptr ||
                    !scene.effective_visible(*item))
                    continue;
                const auto bounds = series_item_bounds(slot.item);
                if (!bounds)
                    continue;
                x_min = std::min(x_min, bounds->x_min());
                x_max = std::max(x_max, bounds->x_max());
                y_min = std::min(y_min, bounds->y_min());
                y_max = std::max(y_max, bounds->y_max());
                found = true;
            }
            return found ? std::optional<tcplot::PlotRange2D>{
                               tcplot::PlotRange2D{x_min, x_max, y_min, y_max}}
                         : std::nullopt;
        }

        void cleanup()
        {
            if (!tc_plot_projection_handle2d_is_invalid(projection_))
            {
                tc_plot_projection2d_destroy(projection_);
                projection_ = tc_plot_projection_handle2d_invalid();
            }
            if (tc_visual_scene_is_valid(scene_))
            {
                if (owns_scene_)
                    tc_visual_scene_destroy(scene_);
                else if (!tc_graphic_item_handle_is_invalid(root_))
                    tc_visual_scene_destroy_item(scene_, root_);
            }
            scene_ = {};
            root_ = tc_graphic_item_handle_invalid();
        }

        tcplot::GpuHost* host_ = nullptr;
        tc_visual_scene_handle scene_{};
        bool owns_scene_ = false;
        tc_plot_rect2d viewport_{};
        tc_plot_rect2d plot_area_{};
        tc_plot_range2d range_{};
        std::string font_uri_;
        float pixel_scale_ = 1.0f;
        tc_chart2d_theme theme_{};
        std::string title_;
        std::string x_label_;
        std::string y_label_;
        uint64_t chart_id_ = next_chart_id.fetch_add(1);
        uint64_t layout_revision_ = 0;
        tcplot::ChartInteraction2D interaction_{};
        tc_plot_projection_handle2d projection_ =
            tc_plot_projection_handle2d_invalid();
        tc_graphic_item_handle root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle background_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle plot_area_root_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle plot_background_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle grid_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle series_root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle annotations_root_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle chrome_root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle x_axis_root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle y_axis_root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle x_axis_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle y_axis_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle x_tick_labels_root_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle y_tick_labels_root_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle title_item_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle x_axis_label_item_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle y_axis_label_item_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle legend_root_ = tc_graphic_item_handle_invalid();
        tc_graphic_item_handle legend_background_ =
            tc_graphic_item_handle_invalid();
        tc_graphic_item_handle overlay_root_ = tc_graphic_item_handle_invalid();
        std::vector<tc_graphic_item_handle> x_tick_labels_;
        std::vector<tc_graphic_item_handle> y_tick_labels_;
        std::vector<SeriesSlot> series_;
        std::vector<uint32_t> free_series_;
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
            tc::Log::error(
                "RetainedChart2D: %s failed: %s", operation, error.what());
        }
        catch (...)
        {
            tc::Log::error("RetainedChart2D: %s failed with unknown error",
                           operation);
        }
        return failure;
    }

} // namespace

struct tc_retained_chart2d
{
    RetainedChart2D value;

    tc_retained_chart2d(tcplot::GpuHost& host,
                        tc_visual_scene_handle scene,
                        bool owns_scene,
                        tc_plot_rect2d viewport,
                        tc_plot_range2d range,
                        std::string font_uri,
                        float pixel_scale,
                        tc_chart2d_theme theme)
        : value(host,
                scene,
                owns_scene,
                viewport,
                range,
                std::move(font_uri),
                pixel_scale,
                theme)
    {
    }
};

extern "C"
{

    tc_chart2d_theme tc_retained_chart2d_default_theme(void)
    {
        return default_theme();
    }

    tc_retained_chart2d*
    tc_retained_chart2d_create(void* gpu_host,
                               tc_plot_rect2d viewport,
                               tc_plot_range2d range,
                               const char* font_uri,
                               float pixel_scale,
                               const tc_chart2d_theme* theme)
    {
        if (!gpu_host)
        {
            tc::Log::error("RetainedChart2D: create requires a live GpuHost");
            return nullptr;
        }
        tc_visual_scene_handle scene = tc_visual_scene_create();
        if (!tc_visual_scene_is_valid(scene))
            return nullptr;
        tc_retained_chart2d* result =
            logged("create",
                   static_cast<tc_retained_chart2d*>(nullptr),
                   [&]
                   {
                       return new tc_retained_chart2d(
                           *static_cast<tcplot::GpuHost*>(gpu_host),
                           scene,
                           true,
                           viewport,
                           range,
                           font_uri ? font_uri : "",
                           pixel_scale,
                           theme ? *theme : default_theme());
                   });
        if (!result && tc_visual_scene_is_valid(scene))
            tc_visual_scene_destroy(scene);
        return result;
    }

    tc_retained_chart2d*
    tc_retained_chart2d_create_in_scene(void* gpu_host,
                                        tc_visual_scene_handle scene,
                                        tc_plot_rect2d viewport,
                                        tc_plot_range2d range,
                                        const char* font_uri,
                                        float pixel_scale,
                                        const tc_chart2d_theme* theme)
    {
        if (!gpu_host)
        {
            tc::Log::error(
                "RetainedChart2D: create_in_scene requires a live GpuHost");
            return nullptr;
        }
        return logged("create_in_scene",
                      static_cast<tc_retained_chart2d*>(nullptr),
                      [&]
                      {
                          return new tc_retained_chart2d(
                              *static_cast<tcplot::GpuHost*>(gpu_host),
                              scene,
                              false,
                              viewport,
                              range,
                              font_uri ? font_uri : "",
                              pixel_scale,
                              theme ? *theme : default_theme());
                      });
    }

    void tc_retained_chart2d_destroy(tc_retained_chart2d* chart)
    {
        delete chart;
    }

    tc_visual_scene_handle
    tc_retained_chart2d_scene(const tc_retained_chart2d* chart)
    {
        return chart ? chart->value.scene() : tc_visual_scene_handle{};
    }

    tc_plot_projection_handle2d
    tc_retained_chart2d_projection(const tc_retained_chart2d* chart)
    {
        return chart ? chart->value.projection()
                     : tc_plot_projection_handle2d_invalid();
    }

    tc_graphic_item_handle
    tc_retained_chart2d_part_handle(const tc_retained_chart2d* chart,
                                    tc_chart2d_part part)
    {
        return chart ? chart->value.part(part)
                     : tc_graphic_item_handle_invalid();
    }

    bool tc_retained_chart2d_layout(const tc_retained_chart2d* chart,
                                    tc_chart2d_layout* out_layout)
    {
        if (!chart || !out_layout)
            return false;
        *out_layout = chart->value.layout();
        return true;
    }

    bool tc_retained_chart2d_range(const tc_retained_chart2d* chart,
                                   tc_plot_range2d* out_range)
    {
        if (!chart || !out_range)
            return false;
        *out_range = chart->value.range();
        return true;
    }

    bool tc_retained_chart2d_theme(const tc_retained_chart2d* chart,
                                   tc_chart2d_theme* out_theme)
    {
        if (!chart || !out_theme)
            return false;
        *out_theme = chart->value.theme();
        return true;
    }

    bool tc_retained_chart2d_set_viewport(tc_retained_chart2d* chart,
                                          tc_plot_rect2d viewport,
                                          float pixel_scale)
    {
        return chart &&
               logged("set_viewport",
                      false,
                      [&]
                      {
                          chart->value.set_viewport(viewport, pixel_scale);
                          return true;
                      });
    }

    bool tc_retained_chart2d_set_range(tc_retained_chart2d* chart,
                                       tc_plot_range2d range)
    {
        return chart && logged("set_range",
                               false,
                               [&]
                               {
                                   chart->value.set_range(range);
                                   return true;
                               });
    }

    bool tc_retained_chart2d_set_frame(tc_retained_chart2d* chart,
                                       tc_plot_rect2d viewport,
                                       float pixel_scale,
                                       tc_plot_range2d range)
    {
        return chart && logged("set_frame",
                               false,
                               [&]
                               {
                                   chart->value.set_frame(
                                       viewport, pixel_scale, range);
                                   return true;
                               });
    }

    bool tc_retained_chart2d_fit(tc_retained_chart2d* chart,
                                 double padding_fraction)
    {
        return chart &&
               logged("fit",
                      false,
                      [&]
                      {
                          chart->value.fit(padding_fraction, true, true);
                          return true;
                      });
    }

    bool tc_retained_chart2d_fit_x(tc_retained_chart2d* chart,
                                   double padding_fraction)
    {
        return chart &&
               logged("fit_x",
                      false,
                      [&]
                      {
                          chart->value.fit(padding_fraction, true, false);
                          return true;
                      });
    }

    bool tc_retained_chart2d_fit_y(tc_retained_chart2d* chart,
                                   double padding_fraction)
    {
        return chart &&
               logged("fit_y",
                      false,
                      [&]
                      {
                          chart->value.fit(padding_fraction, false, true);
                          return true;
                      });
    }

    bool tc_retained_chart2d_pointer_down(tc_retained_chart2d* chart,
                                          float x,
                                          float y,
                                          int button)
    {
        return chart &&
               logged("pointer_down",
                      false,
                      [&] { return chart->value.pointer_down(x, y, button); });
    }

    bool tc_retained_chart2d_pointer_move(tc_retained_chart2d* chart,
                                          float x,
                                          float y)
    {
        return chart && logged("pointer_move",
                               false,
                               [&] { return chart->value.pointer_move(x, y); });
    }

    bool tc_retained_chart2d_pointer_up(tc_retained_chart2d* chart,
                                        float,
                                        float,
                                        int button)
    {
        return chart && logged("pointer_up",
                               false,
                               [&] { return chart->value.pointer_up(button); });
    }

    bool tc_retained_chart2d_wheel(
        tc_retained_chart2d* chart, float x, float y, float steps, bool x_only)
    {
        return chart &&
               logged("wheel",
                      false,
                      [&] { return chart->value.wheel(x, y, steps, x_only); });
    }

    void tc_retained_chart2d_cancel_interaction(tc_retained_chart2d* chart)
    {
        if (chart)
            chart->value.cancel_interaction();
    }

    bool tc_retained_chart2d_set_theme(tc_retained_chart2d* chart,
                                       const tc_chart2d_theme* theme)
    {
        return chart && theme &&
               logged("set_theme",
                      false,
                      [&]
                      {
                          chart->value.set_theme(*theme);
                          return true;
                      });
    }

    bool tc_retained_chart2d_set_title(tc_retained_chart2d* chart,
                                       const char* utf8)
    {
        return chart && logged("set_title",
                               false,
                               [&]
                               {
                                   chart->value.set_title(utf8 ? utf8 : "");
                                   return true;
                               });
    }

    bool tc_retained_chart2d_set_x_axis_label(tc_retained_chart2d* chart,
                                              const char* utf8)
    {
        return chart && logged("set_x_axis_label",
                               false,
                               [&]
                               {
                                   chart->value.set_x_label(utf8 ? utf8 : "");
                                   return true;
                               });
    }

    bool tc_retained_chart2d_set_y_axis_label(tc_retained_chart2d* chart,
                                              const char* utf8)
    {
        return chart && logged("set_y_axis_label",
                               false,
                               [&]
                               {
                                   chart->value.set_y_label(utf8 ? utf8 : "");
                                   return true;
                               });
    }

    bool tc_retained_chart2d_replace_part(tc_retained_chart2d* chart,
                                          tc_chart2d_part part,
                                          tc_graphic_item_handle replacement)
    {
        return chart &&
               logged("replace_part",
                      false,
                      [&]
                      { return chart->value.replace_part(part, replacement); });
    }

    bool tc_retained_chart2d_remove_part(tc_retained_chart2d* chart,
                                         tc_chart2d_part part)
    {
        return chart && logged("remove_part",
                               false,
                               [&] { return chart->value.remove_part(part); });
    }

    tc_graphic_item_handle
    tc_retained_chart2d_add_line(tc_retained_chart2d* chart,
                                 const double* x,
                                 const double* y,
                                 const double* scalar,
                                 size_t point_count,
                                 tc_plot_line_style_state2d style)
    {
        return chart ? logged("add_line",
                              tc_graphic_item_handle_invalid(),
                              [&] {
                                  return chart->value.add_line(
                                      x, y, scalar, point_count, style);
                              })
                     : tc_graphic_item_handle_invalid();
    }

    tc_graphic_item_handle
    tc_retained_chart2d_add_scatter(tc_retained_chart2d* chart,
                                    const double* x,
                                    const double* y,
                                    size_t point_count,
                                    tc_plot_scatter_style_state2d style)
    {
        return chart ? logged("add_scatter",
                              tc_graphic_item_handle_invalid(),
                              [&] {
                                  return chart->value.add_scatter(
                                      x, y, point_count, style);
                              })
                     : tc_graphic_item_handle_invalid();
    }

    bool tc_retained_chart2d_remove_series(tc_retained_chart2d* chart,
                                           tc_graphic_item_handle series)
    {
        return chart &&
               logged("remove_series",
                      false,
                      [&] { return chart->value.remove_series(series); });
    }

    tc_chart_series_handle2d
    tc_retained_chart2d_add_named_line(tc_retained_chart2d* chart,
                                       const char* name_utf8,
                                       bool show_in_legend,
                                       const double* x,
                                       const double* y,
                                       const double* scalar,
                                       size_t point_count,
                                       tc_plot_line_style_state2d style)
    {
        return chart ? logged("add_named_line",
                              invalid_series(),
                              [&]
                              {
                                  return chart->value.add_named_line(
                                      name_utf8 ? name_utf8 : "",
                                      show_in_legend,
                                      x,
                                      y,
                                      scalar,
                                      point_count,
                                      style);
                              })
                     : invalid_series();
    }

    tc_chart_series_handle2d
    tc_retained_chart2d_add_named_scatter(
        tc_retained_chart2d* chart,
        const char* name_utf8,
        bool show_in_legend,
        const double* x,
        const double* y,
        size_t point_count,
        tc_plot_scatter_style_state2d style)
    {
        return chart ? logged("add_named_scatter",
                              invalid_series(),
                              [&]
                              {
                                  return chart->value.add_named_scatter(
                                      name_utf8 ? name_utf8 : "",
                                      show_in_legend,
                                      x,
                                      y,
                                      point_count,
                                      style);
                              })
                     : invalid_series();
    }

    bool tc_retained_chart2d_series_is_valid(
        const tc_retained_chart2d* chart, tc_chart_series_handle2d series)
    {
        return chart && chart->value.series_valid(series);
    }

    bool tc_retained_chart2d_series_snapshot(
        const tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        tc_chart_series_snapshot2d* out_snapshot)
    {
        return chart && out_snapshot &&
               chart->value.series_snapshot(series, *out_snapshot);
    }

    size_t tc_retained_chart2d_series_name_copy(
        const tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        char* out_utf8,
        size_t capacity)
    {
        if (!chart)
            return 0;
        const std::string* name = chart->value.series_name(series);
        if (!name)
            return 0;
        const size_t required = name->size() + 1;
        if (!out_utf8)
            return required;
        if (capacity < required)
        {
            tc::Log::error(
                "RetainedChart2D: series_name_copy output is too small");
            return 0;
        }
        std::memcpy(out_utf8, name->c_str(), required);
        return required;
    }

    bool tc_retained_chart2d_series_set_name(
        tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        const char* name_utf8)
    {
        return chart && logged("series_set_name",
                               false,
                               [&]
                               {
                                   return chart->value.set_series_name(
                                       series, name_utf8 ? name_utf8 : "");
                               });
    }

    bool tc_retained_chart2d_series_set_visible(
        tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        bool visible)
    {
        return chart && logged("series_set_visible",
                               false,
                               [&]
                               {
                                   return chart->value.set_series_visible(
                                       series, visible);
                               });
    }

    bool tc_retained_chart2d_series_set_legend_visible(
        tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        bool show_in_legend)
    {
        return chart && logged("series_set_legend_visible",
                               false,
                               [&]
                               {
                                   return chart->value
                                       .set_series_legend_visible(
                                           series, show_in_legend);
                               });
    }

    bool tc_retained_chart2d_line_series_set_style(
        tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        tc_plot_line_style_state2d style)
    {
        return chart && logged("line_series_set_style",
                               false,
                               [&]
                               {
                                   return chart->value.set_line_series_style(
                                       series, style);
                               });
    }

    bool tc_retained_chart2d_scatter_series_set_style(
        tc_retained_chart2d* chart,
        tc_chart_series_handle2d series,
        tc_plot_scatter_style_state2d style)
    {
        return chart && logged(
                            "scatter_series_set_style",
                            false,
                            [&]
                            {
                                return chart->value.set_scatter_series_style(
                                    series, style);
                            });
    }

    bool tc_retained_chart2d_remove_semantic_series(
        tc_retained_chart2d* chart, tc_chart_series_handle2d series)
    {
        return chart && logged("remove_semantic_series",
                               false,
                               [&]
                               {
                                   return chart->value.remove_semantic_series(
                                       series);
                               });
    }

} // extern "C"
