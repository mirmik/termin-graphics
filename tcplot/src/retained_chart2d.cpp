#include "tcplot/retained_chart2d.h"

#include <algorithm>
#include <cmath>
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
            tc_graphic_item_handle item = tc_plot_line_series_item2d_create(
                scene_, projection_, x, y, scalar, count, style);
            return adopt_series(item);
        }

        tc_graphic_item_handle add_scatter(const double* x,
                                           const double* y,
                                           size_t count,
                                           tc_plot_scatter_style_state2d style)
        {
            tc_graphic_item_handle item = tc_plot_scatter_series_item2d_create(
                scene_, projection_, x, y, count, style);
            return adopt_series(item);
        }

        bool remove_series(tc_graphic_item_handle item)
        {
            if ((!tc_visual_scene_item_is_type(scene_, item, kLineType) &&
                 !tc_visual_scene_item_is_type(scene_, item, kScatterType)) ||
                !same(tc_visual_scene_item_parent(scene_, item), series_root_))
                return false;
            return tc_visual_scene_destroy_item(scene_, item);
        }

    private:
        struct PartSlot
        {
            tc_graphic_item_handle* handle = nullptr;
            tc_graphic_item_handle* parent = nullptr;
            const char* type = nullptr;
            int64_t z_order = 0;
        };

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
            overlay_root_ = create_group(root_);

            tc_visual_scene_item_set_z_order(scene_, background_, -100);
            tc_visual_scene_item_set_z_order(scene_, plot_area_root_, 0);
            tc_visual_scene_item_set_z_order(scene_, plot_background_, 0);
            tc_visual_scene_item_set_z_order(scene_, grid_, 10);
            tc_visual_scene_item_set_z_order(scene_, series_root_, 20);
            tc_visual_scene_item_set_z_order(scene_, annotations_root_, 30);
            tc_visual_scene_item_set_z_order(scene_, chrome_root_, 40);
            tc_visual_scene_item_set_z_order(scene_, legend_root_, 50);
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

        std::optional<tcplot::PlotRange2D> series_bounds() const
        {
            double x_min = std::numeric_limits<double>::infinity();
            double x_max = -std::numeric_limits<double>::infinity();
            double y_min = std::numeric_limits<double>::infinity();
            double y_max = -std::numeric_limits<double>::infinity();
            bool found = false;
            termin::visual::TcVisualScene scene{scene_};
            const size_t count =
                tc_visual_scene_item_child_count(scene_, series_root_);
            for (size_t index = 0; index < count; ++index)
            {
                const tc_graphic_item_handle handle =
                    tc_visual_scene_item_child_at(scene_, series_root_, index);
                const tc_graphic_item* item = scene.resolve(handle);
                if (item == nullptr || !scene.effective_visible(*item))
                    continue;
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
                {
                    continue;
                }
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
            }
            if (!found)
                return std::nullopt;
            return tcplot::PlotRange2D{x_min, x_max, y_min, y_max};
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
        uint64_t layout_revision_ = 0;
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
        tc_graphic_item_handle overlay_root_ = tc_graphic_item_handle_invalid();
        std::vector<tc_graphic_item_handle> x_tick_labels_;
        std::vector<tc_graphic_item_handle> y_tick_labels_;
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

} // extern "C"
