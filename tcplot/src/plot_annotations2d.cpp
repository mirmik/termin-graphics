#include "tcplot/plot_annotations2d.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.hpp>
#include <termin/geom/affine2.hpp>
#include <termin_visual_scene/builtin_items2d.hpp>
#include <termin_visual_scene/scene_render2d.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/render_context.hpp>

namespace tcplot {
    namespace {

        using termin::visual::GraphicItemHandle;
        using termin::visual::PointerDispatch2D;
        using termin::visual::PointerEvent2D;
        using termin::visual::PointerEventKind2D;

        constexpr std::size_t kPhaseCount = 3;
        constexpr std::size_t kClipCount = 2;
        constexpr std::size_t kBucketCount = kPhaseCount * kClipCount;
        std::atomic<std::uint64_t> g_next_annotation_layer_id{1};

        std::size_t bucket_index(PlotAnnotationPhase2D phase, PlotAnnotationClip2D clip) {
            return static_cast<std::size_t>(phase) * kClipCount + static_cast<std::size_t>(clip);
        }

        bool same_graphic_handle(GraphicItemHandle lhs, GraphicItemHandle rhs) {
            return lhs.scene_id == rhs.scene_id && lhs.index == rhs.index && lhs.generation == rhs.generation;
        }

        bool invalid_graphic_handle(GraphicItemHandle value) {
            return tc_graphic_item_handle_is_invalid(value);
        }

        bool finite(PlotPixelPoint2D point) {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        bool finite(termin::SrgbColor color) {
            return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b) &&
                   std::isfinite(color.a);
        }

        bool valid_data_marker(const PlotDataMarker2D& marker) {
            return std::isfinite(marker.data_position.x) && std::isfinite(marker.data_position.y) &&
                   std::isfinite(marker.callout_offset.x) && std::isfinite(marker.callout_offset.y) &&
                   std::isfinite(marker.callout_width) && std::isfinite(marker.callout_height) &&
                   std::isfinite(marker.anchor_radius) && std::isfinite(marker.text_size) &&
                   marker.callout_width > 0.0f && marker.callout_height > 0.0f && marker.anchor_radius > 0.0f &&
                   marker.text_size > 0.0f && finite(marker.anchor_color) && finite(marker.hover_color) &&
                   finite(marker.callout_color) && finite(marker.border_color) && finite(marker.text_color);
        }

        std::optional<PlotPixelPoint2D>
        resolve_anchor(const PlotAnchor2D& anchor, const PlotFrame2D& frame, const PlotData& data) {
            return std::visit(
                [&](const auto& value) -> std::optional<PlotPixelPoint2D> {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, DataAnchor2D>) {
                        const auto result = frame.data_to_pixel(value.x, value.y);
                        return finite(result) ? std::optional<PlotPixelPoint2D>(result) : std::nullopt;
                    } else if constexpr (std::is_same_v<T, SeriesPointRef2D>) {
                        const std::vector<double>* x = nullptr;
                        const std::vector<double>* y = nullptr;
                        if (value.series_kind == PlotSeriesKind2D::Line) {
                            if (value.series_index >= data.lines.size()) {
                                return std::nullopt;
                            }
                            x = &data.lines[value.series_index].x;
                            y = &data.lines[value.series_index].y;
                        } else {
                            if (value.series_index >= data.scatters.size()) {
                                return std::nullopt;
                            }
                            x = &data.scatters[value.series_index].x;
                            y = &data.scatters[value.series_index].y;
                        }
                        if (value.point_index >= x->size() || value.point_index >= y->size()) {
                            return std::nullopt;
                        }
                        const auto result = frame.data_to_pixel((*x)[value.point_index], (*y)[value.point_index]);
                        return finite(result) ? std::optional<PlotPixelPoint2D>(result) : std::nullopt;
                    } else if constexpr (std::is_same_v<T, AxesFractionAnchor2D>) {
                        const PlotRect2D& area = frame.plot_area();
                        const PlotPixelPoint2D result{
                            area.x() + static_cast<float>(value.x) * area.width(),
                            area.bottom() - static_cast<float>(value.y) * area.height(),
                        };
                        return finite(result) ? std::optional<PlotPixelPoint2D>(result) : std::nullopt;
                    } else {
                        const PlotPixelPoint2D result{
                            frame.viewport().x() + value.x,
                            frame.viewport().y() + value.y,
                        };
                        return finite(result) ? std::optional<PlotPixelPoint2D>(result) : std::nullopt;
                    }
                },
                anchor);
        }

        PlotAnnotationPhase2D annotation_phase(PlotRenderPhase2D phase) {
            switch (phase) {
            case PlotRenderPhase2D::AnnotationUnderlay:
                return PlotAnnotationPhase2D::Underlay;
            case PlotRenderPhase2D::AnnotationOverlay:
                return PlotAnnotationPhase2D::Overlay;
            case PlotRenderPhase2D::UnclippedChrome:
                return PlotAnnotationPhase2D::Chrome;
            default:
                throw std::logic_error("render phase does not contain plot annotations");
            }
        }

    } // namespace

    struct PlotAnnotationLayer2D::Impl {
        struct Projection {
            GraphicItemHandle item = tc_graphic_item_handle_invalid();
            termin::visual::GraphicItem2D* object = nullptr;
            std::size_t visual_index = 0;
            std::size_t bucket = 0;
        };

        struct Record {
            struct MarkerRuntime {
                PlotDataMarker2D marker;
                bool hovered = false;
                bool dragging = false;
                termin::visual::PointerId2D drag_pointer = 0;
            };

            std::uint32_t generation = 1;
            bool alive = false;
            PlotAnnotation2D annotation;
            std::optional<PlotPixelPoint2D> projected_anchor;
            std::vector<Projection> projections;
            SnapHook snap_hook;
            ActionHandler action_handler;
            std::optional<MarkerRuntime> marker;
        };

        class Resources final : public termin::visual::SceneRenderResourceResolver2D,
                                public tgfx::DrawResourceResolver2D {
        public:
            tgfx::FontAtlas* font = nullptr;

            std::optional<tgfx::FontHandle> resolve_font(std::string_view uri) override {
                if (uri != kPlotDefaultFontResource2D || !font) {
                    tc::Log::error("PlotAnnotationLayer2D: unresolved font resource '%s'", std::string(uri).c_str());
                    return std::nullopt;
                }
                return tgfx::FontHandle{1};
            }

            std::optional<tgfx::TextureHandle> resolve_image(std::string_view uri) override {
                tc::Log::error("PlotAnnotationLayer2D: unresolved image resource '%s'", std::string(uri).c_str());
                return std::nullopt;
            }

            std::optional<termin::visual::ResolvedCustomBatch2D> resolve_custom_batch(std::string_view key,
                                                                                      termin::Bounds2f) override {
                tc::Log::error("PlotAnnotationLayer2D: unresolved custom batch '%s'", std::string(key).c_str());
                return std::nullopt;
            }

            tgfx::FontAtlas* resolve_font(tgfx::FontHandle handle) override {
                return handle.id == 1 ? font : nullptr;
            }
        };

        struct Bucket {
            Bucket()
                : scene(tc_visual_scene_create()) {}
            ~Bucket() {
                tc_visual_scene_destroy(scene.handle());
            }
            termin::visual::TcVisualScene scene;
            termin::visual::SceneInteraction2D interaction;
        };

        std::uint64_t id = g_next_annotation_layer_id.fetch_add(1);
        std::vector<Record> records;
        std::vector<std::uint32_t> free_indices;
        std::deque<PlotAnnotationAction2D> pending_actions;
        std::array<Bucket, kBucketCount> buckets;
        tgfx::Canvas2DRenderer canvas;
        Resources resources;

        Record* resolve(PlotAnnotationHandle handle) {
            if (handle.layer_id != id || handle.index >= records.size()) {
                return nullptr;
            }
            Record& record = records[handle.index];
            return record.alive && record.generation == handle.generation ? &record : nullptr;
        }

        const Record* resolve(PlotAnnotationHandle handle) const {
            if (handle.layer_id != id || handle.index >= records.size()) {
                return nullptr;
            }
            const Record& record = records[handle.index];
            return record.alive && record.generation == handle.generation ? &record : nullptr;
        }

        PlotAnnotationHandle handle(std::uint32_t index) const {
            return {id, index, records[index].generation};
        }

        void clear_projection(Record& record) {
            for (const Projection& projection : record.projections) {
                Bucket& bucket = buckets[projection.bucket];
                bucket.interaction.cancel_all();
                bucket.interaction.clear_action_handler(projection.item);
                if (!bucket.scene.destroy(projection.item)) {
                    tc::Log::error("PlotAnnotationLayer2D: failed to destroy projected item");
                }
            }
            record.projections.clear();
            record.projected_anchor.reset();
        }

        void rebuild_marker_visuals(Record& record) {
            if (!record.marker)
                return;
            const PlotDataMarker2D& marker = record.marker->marker;
            const bool highlighted = record.marker->hovered || record.marker->dragging;
            const termin::SrgbColor accent = highlighted ? marker.hover_color : marker.anchor_color;
            const float radius = marker.anchor_radius + (highlighted ? 1.5f : 0.0f);
            const float half_width = marker.callout_width * 0.5f;
            const float half_height = marker.callout_height * 0.5f;

            record.annotation.anchor = DataAnchor2D{
                marker.data_position.x,
                marker.data_position.y,
            };
            record.annotation.visuals.clear();

            PlotAnnotationVisual2D anchor;
            anchor.item = std::make_unique<termin::visual::EllipseItem2D>(
                termin::Rect2f{-radius, -radius, radius * 2.0f, radius * 2.0f},
                tgfx::FillPaint{termin::srgb_to_linear(accent)},
                tgfx::StrokePaint{
                    termin::srgb_to_linear(marker.border_color),
                    highlighted ? 2.5f : 1.5f,
                });
            anchor.phase = PlotAnnotationPhase2D::Overlay;
            anchor.clip = PlotAnnotationClip2D::PlotArea;
            anchor.z_order = 20;
            record.annotation.visuals.push_back(std::move(anchor));

            PlotAnnotationVisual2D leader;
            leader.item = std::make_unique<termin::visual::PolylineItem2D>(
                std::vector<termin::Vec2f>{{0.0f, 0.0f}, marker.callout_offset},
                tgfx::StrokePaint{termin::srgb_to_linear(accent), highlighted ? 2.5f : 1.5f},
                false);
            leader.phase = PlotAnnotationPhase2D::Overlay;
            leader.clip = PlotAnnotationClip2D::PlotArea;
            leader.z_order = 10;
            leader.enabled = false;
            record.annotation.visuals.push_back(std::move(leader));

            PlotAnnotationVisual2D bubble;
            bubble.item = std::make_unique<termin::visual::RoundedRectItem2D>(
                termin::Rect2f{-half_width, -half_height, marker.callout_width, marker.callout_height},
                8.0f,
                tgfx::FillPaint{termin::srgb_to_linear(marker.callout_color)},
                tgfx::StrokePaint{
                    termin::srgb_to_linear(highlighted ? accent : marker.border_color),
                    highlighted ? 2.0f : 1.0f,
                });
            bubble.pixel_offset = marker.callout_offset;
            bubble.phase = PlotAnnotationPhase2D::Chrome;
            bubble.clip = PlotAnnotationClip2D::PlotArea;
            bubble.z_order = 30;
            record.annotation.visuals.push_back(std::move(bubble));

            PlotAnnotationVisual2D text;
            text.item = std::make_unique<termin::visual::TextItem2D>(
                marker.text,
                std::string(kPlotDefaultFontResource2D),
                termin::Vec2f{-half_width + 12.0f, 5.0f},
                marker.text_size,
                marker.text_color,
                tgfx::TextAnchor2D::Left,
                termin::Bounds2f{-half_width + 12.0f, -half_height + 7.0f, half_width - 30.0f, half_height - 7.0f});
            text.pixel_offset = marker.callout_offset;
            text.phase = PlotAnnotationPhase2D::Chrome;
            text.clip = PlotAnnotationClip2D::PlotArea;
            text.z_order = 31;
            text.enabled = false;
            record.annotation.visuals.push_back(std::move(text));

            if (marker.close_button) {
                PlotAnnotationVisual2D close;
                close.item = std::make_unique<termin::visual::RoundedRectItem2D>(
                    termin::Rect2f{-8.0f, -8.0f, 16.0f, 16.0f},
                    4.0f,
                    tgfx::FillPaint{termin::srgb_to_linear(highlighted ? termin::SrgbColor{0.85f, 0.25f, 0.22f, 1.0f}
                                                : termin::SrgbColor{0.48f, 0.20f, 0.20f, 1.0f})},
                    std::nullopt);
                close.pixel_offset = {
                    marker.callout_offset.x + half_width - 14.0f,
                    marker.callout_offset.y,
                };
                close.phase = PlotAnnotationPhase2D::Chrome;
                close.clip = PlotAnnotationClip2D::PlotArea;
                close.z_order = 32;
                close.action = "close";
                record.annotation.visuals.push_back(std::move(close));
            }
        }

        std::optional<std::pair<std::uint32_t, std::size_t>> find_projection(GraphicItemHandle item) const {
            if (invalid_graphic_handle(item))
                return std::nullopt;
            for (std::uint32_t i = 0; i < records.size(); ++i) {
                const Record& record = records[i];
                if (!record.alive)
                    continue;
                for (const Projection& projection : record.projections) {
                    if (same_graphic_handle(projection.item, item)) {
                        return std::pair{i, projection.visual_index};
                    }
                }
            }
            return std::nullopt;
        }

        bool destroy_record(PlotAnnotationHandle handle) {
            Record* record = resolve(handle);
            if (!record)
                return false;
            clear_projection(*record);
            record->alive = false;
            record->annotation = {};
            record->snap_hook = {};
            record->action_handler = {};
            record->marker.reset();
            ++record->generation;
            if (record->generation == 0)
                ++record->generation;
            free_indices.push_back(handle.index);
            return true;
        }

        void
        bind_action(std::uint32_t record_index, const Projection& projection, const PlotAnnotationVisual2D& visual) {
            buckets[projection.bucket].interaction.set_action_handler(
                projection.item,
                [this,
                 record_index,
                 generation = records[record_index].generation,
                 visual_index = projection.visual_index,
                 action = visual.action](const termin::visual::ActionEvent2D& event) {
                    const PlotAnnotationHandle annotation{id, record_index, generation};
                    Record* record = resolve(annotation);
                    if (!record)
                        return;
                    pending_actions.push_back({
                        annotation,
                        visual_index,
                        event.pointer,
                        action,
                    });
                    if (record->action_handler) {
                        try {
                            record->action_handler({
                                annotation,
                                visual_index,
                                event.pointer,
                                action,
                            });
                        } catch (const std::exception& error) {
                            tc::Log::error("PlotAnnotationLayer2D: action handler failed: %s", error.what());
                            throw;
                        } catch (...) {
                            tc::Log::error("PlotAnnotationLayer2D: action handler failed");
                            throw;
                        }
                    }
                    record = resolve(annotation);
                    if (record && record->marker && action == "close") {
                        destroy_record(annotation);
                    }
                });
        }

        bool projection_topology_matches(const Record& record) const {
            if (record.projections.size() != record.annotation.visuals.size()) {
                return false;
            }
            for (std::size_t i = 0; i < record.projections.size(); ++i) {
                const auto& visual = record.annotation.visuals[i];
                if (record.projections[i].visual_index != i ||
                    record.projections[i].bucket != bucket_index(visual.phase, visual.clip)) {
                    return false;
                }
            }
            return true;
        }

        bool materialize_visuals(std::uint32_t record_index) {
            Record& record = records[record_index];
            if (!projection_topology_matches(record)) {
                clear_projection(record);
            }
            if (record.projections.empty()) {
                record.projections.reserve(record.annotation.visuals.size());
                for (std::size_t i = 0; i < record.annotation.visuals.size(); ++i) {
                    auto& visual = record.annotation.visuals[i];
                    if (!visual.item) {
                        tc::Log::error("PlotAnnotationLayer2D: visual has no item");
                        clear_projection(record);
                        return false;
                    }
                    const std::size_t bucket = bucket_index(visual.phase, visual.clip);
                    auto* object = visual.item.get();
                    const auto handle = buckets[bucket].scene.adopt(std::move(visual.item));
                    if (!handle) {
                        tc::Log::error("PlotAnnotationLayer2D: failed to adopt projected item");
                        clear_projection(record);
                        return false;
                    }
                    record.projections.push_back({*handle, object, i, bucket});
                    bind_action(record_index, record.projections.back(), visual);
                }
                return true;
            }

            for (std::size_t i = 0; i < record.projections.size(); ++i) {
                auto& visual = record.annotation.visuals[i];
                if (!visual.item)
                    continue;
                auto* object = visual.item.get();
                auto& projection = record.projections[i];
                if (!buckets[projection.bucket].scene.replace(projection.item, std::move(visual.item))) {
                    tc::Log::error("PlotAnnotationLayer2D: failed to replace projected item");
                    clear_projection(record);
                    return false;
                }
                projection.object = object;
            }
            return true;
        }

        void project_record(std::uint32_t record_index, const PlotFrame2D& frame, const PlotData& data) {
            Record& record = records[record_index];
            const auto anchor = resolve_anchor(record.annotation.anchor, frame, data);
            if (!materialize_visuals(record_index)) {
                return;
            }

            for (const Projection& projection : record.projections) {
                const PlotAnnotationVisual2D& visual = record.annotation.visuals[projection.visual_index];
                termin::Affine2f transform{};
                if (anchor) {
                    transform = termin::Affine2f::translation(anchor->x + visual.pixel_offset.x,
                                                              anchor->y + visual.pixel_offset.y);
                }
                projection.object->set_local_transform(transform);
                projection.object->set_z_order(visual.z_order);
                projection.object->set_visible(visual.visible && anchor.has_value());
                projection.object->set_enabled(visual.enabled);
            }
            record.projected_anchor = anchor;
        }

        void set_marker_hover(std::optional<std::uint32_t> hovered, const PlotFrame2D& frame) {
            static const PlotData empty_data;
            for (std::uint32_t i = 0; i < records.size(); ++i) {
                Record& record = records[i];
                if (!record.alive || !record.marker)
                    continue;
                const bool value = hovered && *hovered == i;
                if (record.marker->hovered == value)
                    continue;
                record.marker->hovered = value;
                rebuild_marker_visuals(record);
                project_record(i, frame, empty_data);
            }
        }

        void process_marker_dispatch(const PlotFrame2D& frame, const PointerDispatch2D& dispatch) {
            const auto hovered = find_projection(dispatch.hovered);
            std::optional<std::uint32_t> hovered_marker;
            if (hovered && records[hovered->first].marker) {
                hovered_marker = hovered->first;
            }
            set_marker_hover(hovered_marker, frame);

            const auto target = find_projection(dispatch.target);
            if (!target)
                return;
            Record& record = records[target->first];
            if (!record.marker)
                return;
            auto& marker = *record.marker;

            if (dispatch.event.kind == PointerEventKind2D::Down && target->second == 0) {
                marker.dragging = true;
                marker.drag_pointer = dispatch.event.pointer;
                rebuild_marker_visuals(record);
            } else if (dispatch.event.kind == PointerEventKind2D::Move && marker.dragging &&
                       marker.drag_pointer == dispatch.event.pointer) {
                PlotPoint2D candidate = frame.pixel_to_data(dispatch.event.position.x, dispatch.event.position.y);
                if (record.snap_hook) {
                    try {
                        candidate = record.snap_hook(candidate);
                    } catch (const std::exception& error) {
                        tc::Log::error("PlotAnnotationLayer2D: marker snap hook failed: %s", error.what());
                        throw;
                    } catch (...) {
                        tc::Log::error("PlotAnnotationLayer2D: marker snap hook failed");
                        throw;
                    }
                }
                marker.marker.data_position = candidate;
                rebuild_marker_visuals(record);
            } else if ((dispatch.event.kind == PointerEventKind2D::Up ||
                        dispatch.event.kind == PointerEventKind2D::Cancel) &&
                       marker.dragging && marker.drag_pointer == dispatch.event.pointer) {
                marker.dragging = false;
                rebuild_marker_visuals(record);
            }

            static const PlotData empty_data;
            if (record.alive) {
                project_record(target->first, frame, empty_data);
            }
        }

        std::vector<std::size_t> front_to_back_buckets() const {
            return {
                bucket_index(PlotAnnotationPhase2D::Chrome, PlotAnnotationClip2D::Viewport),
                bucket_index(PlotAnnotationPhase2D::Chrome, PlotAnnotationClip2D::PlotArea),
                bucket_index(PlotAnnotationPhase2D::Overlay, PlotAnnotationClip2D::Viewport),
                bucket_index(PlotAnnotationPhase2D::Overlay, PlotAnnotationClip2D::PlotArea),
                bucket_index(PlotAnnotationPhase2D::Underlay, PlotAnnotationClip2D::Viewport),
                bucket_index(PlotAnnotationPhase2D::Underlay, PlotAnnotationClip2D::PlotArea),
            };
        }

        bool bucket_is_plot_clipped(std::size_t bucket) const {
            return bucket % kClipCount == static_cast<std::size_t>(PlotAnnotationClip2D::PlotArea);
        }
    };

    PlotAnnotationLayer2D::PlotAnnotationLayer2D()
        : impl_(std::make_unique<Impl>()) {}

    PlotAnnotationLayer2D::~PlotAnnotationLayer2D() = default;

    std::optional<PlotAnnotationHandle> PlotAnnotationLayer2D::create(PlotAnnotation2D annotation) {
        std::uint32_t index = 0;
        if (impl_->free_indices.empty()) {
            index = static_cast<std::uint32_t>(impl_->records.size());
            impl_->records.emplace_back();
        } else {
            index = impl_->free_indices.back();
            impl_->free_indices.pop_back();
        }
        Impl::Record& record = impl_->records[index];
        record.alive = true;
        record.annotation = std::move(annotation);
        record.projected_anchor.reset();
        record.projections.clear();
        record.snap_hook = {};
        record.action_handler = {};
        record.marker.reset();
        return impl_->handle(index);
    }

    bool PlotAnnotationLayer2D::update(PlotAnnotationHandle handle, PlotAnnotation2D annotation) {
        Impl::Record* record = impl_->resolve(handle);
        if (!record)
            return false;
        if (record->marker) {
            tc::Log::error("PlotAnnotationLayer2D: generic update rejected for data marker");
            return false;
        }
        impl_->clear_projection(*record);
        record->annotation = std::move(annotation);
        return true;
    }

    bool PlotAnnotationLayer2D::destroy(PlotAnnotationHandle handle) {
        return impl_->destroy_record(handle);
    }

    void PlotAnnotationLayer2D::clear() {
        for (std::uint32_t i = 0; i < impl_->records.size(); ++i) {
            if (!impl_->records[i].alive)
                continue;
            destroy(impl_->handle(i));
        }
        impl_->pending_actions.clear();
    }

    std::optional<PlotAnnotationSnapshot2D> PlotAnnotationLayer2D::snapshot(PlotAnnotationHandle handle) const {
        const Impl::Record* record = impl_->resolve(handle);
        if (!record)
            return std::nullopt;
        PlotAnnotationSnapshot2D result;
        result.handle = handle;
        result.anchor = record->annotation.anchor;
        result.projected_anchor = record->projected_anchor;
        result.projected_graphics.reserve(record->projections.size());
        for (const Impl::Projection& projection : record->projections) {
            const auto& visual = record->annotation.visuals[projection.visual_index];
            result.projected_graphics.push_back({
                projection.item,
                projection.visual_index,
                visual.phase,
                visual.clip,
            });
        }
        return result;
    }

    std::vector<PlotAnnotationSnapshot2D> PlotAnnotationLayer2D::snapshots() const {
        std::vector<PlotAnnotationSnapshot2D> result;
        result.reserve(size());
        for (std::uint32_t i = 0; i < impl_->records.size(); ++i) {
            if (!impl_->records[i].alive)
                continue;
            result.push_back(*snapshot(impl_->handle(i)));
        }
        return result;
    }

    std::size_t PlotAnnotationLayer2D::size() const {
        return static_cast<std::size_t>(std::count_if(
            impl_->records.begin(), impl_->records.end(), [](const Impl::Record& record) { return record.alive; }));
    }

    bool PlotAnnotationLayer2D::set_snap_hook(PlotAnnotationHandle handle, SnapHook hook) {
        Impl::Record* record = impl_->resolve(handle);
        if (!record)
            return false;
        record->snap_hook = std::move(hook);
        return true;
    }

    std::optional<PlotPoint2D> PlotAnnotationLayer2D::snap_data(PlotAnnotationHandle handle,
                                                                PlotPoint2D candidate) const {
        const Impl::Record* record = impl_->resolve(handle);
        if (!record)
            return std::nullopt;
        if (!record->snap_hook)
            return candidate;
        try {
            return record->snap_hook(candidate);
        } catch (const std::exception& error) {
            tc::Log::error("PlotAnnotationLayer2D: snap hook failed: %s", error.what());
            throw;
        } catch (...) {
            tc::Log::error("PlotAnnotationLayer2D: snap hook failed");
            throw;
        }
    }

    bool PlotAnnotationLayer2D::set_action_handler(PlotAnnotationHandle handle, ActionHandler handler) {
        Impl::Record* record = impl_->resolve(handle);
        if (!record)
            return false;
        record->action_handler = std::move(handler);
        return true;
    }

    std::optional<PlotAnnotationAction2D> PlotAnnotationLayer2D::take_action() {
        if (impl_->pending_actions.empty())
            return std::nullopt;
        PlotAnnotationAction2D result = std::move(impl_->pending_actions.front());
        impl_->pending_actions.pop_front();
        return result;
    }

    std::optional<PlotAnnotationHandle> PlotAnnotationLayer2D::create_data_marker(PlotDataMarker2D marker) {
        if (!valid_data_marker(marker)) {
            tc::Log::error("PlotAnnotationLayer2D: rejected invalid data marker");
            return std::nullopt;
        }

        const auto handle = create({});
        if (!handle)
            return std::nullopt;
        Impl::Record* record = impl_->resolve(*handle);
        record->marker = Impl::Record::MarkerRuntime{std::move(marker)};
        impl_->rebuild_marker_visuals(*record);
        return handle;
    }

    bool PlotAnnotationLayer2D::update_data_marker(PlotAnnotationHandle handle, PlotDataMarker2D marker) {
        Impl::Record* record = impl_->resolve(handle);
        if (!record || !record->marker)
            return false;
        if (!valid_data_marker(marker)) {
            tc::Log::error("PlotAnnotationLayer2D: rejected invalid data marker update");
            return false;
        }
        record->marker->marker = std::move(marker);
        impl_->rebuild_marker_visuals(*record);
        return true;
    }

    std::optional<PlotDataMarkerSnapshot2D>
    PlotAnnotationLayer2D::data_marker_snapshot(PlotAnnotationHandle handle) const {
        const Impl::Record* record = impl_->resolve(handle);
        if (!record || !record->marker)
            return std::nullopt;
        return PlotDataMarkerSnapshot2D{
            handle,
            record->marker->marker,
            record->marker->hovered,
            record->marker->dragging,
        };
    }

    void PlotAnnotationLayer2D::project(const PlotFrame2D& frame, const PlotData& data) {
        for (std::uint32_t i = 0; i < impl_->records.size(); ++i) {
            if (!impl_->records[i].alive)
                continue;
            impl_->project_record(i, frame, data);
        }
    }

    bool PlotAnnotationLayer2D::route_pointer(const PlotFrame2D& frame, const PointerEvent2D& event) {
        const auto order = impl_->front_to_back_buckets();

        // Captured interaction owns move/up even after the pointer leaves its
        // clipping region.
        for (const std::size_t bucket : order) {
            if (!invalid_graphic_handle(impl_->buckets[bucket].interaction.captured(event.pointer))) {
                const PointerDispatch2D dispatch =
                    impl_->buckets[bucket].interaction.route(impl_->buckets[bucket].scene, event);
                impl_->process_marker_dispatch(frame, dispatch);
                return !invalid_graphic_handle(dispatch.target);
            }
        }

        for (const std::size_t bucket : order) {
            if (impl_->bucket_is_plot_clipped(bucket) &&
                !frame.contains_plot_pixel(event.position.x, event.position.y)) {
                continue;
            }
            const PointerDispatch2D dispatch =
                impl_->buckets[bucket].interaction.route(impl_->buckets[bucket].scene, event);
            if (!invalid_graphic_handle(dispatch.target)) {
                impl_->process_marker_dispatch(frame, dispatch);
                return true;
            }
        }
        impl_->set_marker_hover(std::nullopt, frame);
        return false;
    }

    bool PlotAnnotationLayer2D::hit_test(const PlotFrame2D& frame, float x, float y) const {
        for (const std::size_t bucket : impl_->front_to_back_buckets()) {
            if (impl_->bucket_is_plot_clipped(bucket) && !frame.contains_plot_pixel(x, y)) {
                continue;
            }
            if (termin::visual::hit_test(impl_->buckets[bucket].scene, {x, y})) {
                return true;
            }
        }
        return false;
    }

    void PlotAnnotationLayer2D::render_phase(PlotRenderPhase2D phase,
                                             const PlotFrame2D& frame,
                                             tgfx::RenderContext2& context,
                                             tgfx::FontAtlas* font) {
        if (phase != PlotRenderPhase2D::AnnotationUnderlay && phase != PlotRenderPhase2D::AnnotationOverlay &&
            phase != PlotRenderPhase2D::UnclippedChrome) {
            return;
        }
        const PlotAnnotationPhase2D annotation = annotation_phase(phase);
        impl_->resources.font = font;

        for (const PlotAnnotationClip2D clip : {PlotAnnotationClip2D::PlotArea, PlotAnnotationClip2D::Viewport}) {
            Impl::Bucket& bucket = impl_->buckets[bucket_index(annotation, clip)];
            if (bucket.scene.size() == 0)
                continue;
            const PlotRect2D& viewport = frame.viewport();
            impl_->canvas.set_default_font(font);
            impl_->canvas.begin(context,
                                static_cast<int>(viewport.x()),
                                static_cast<int>(viewport.y()),
                                static_cast<int>(viewport.width()),
                                static_cast<int>(viewport.height()));
            const PlotRect2D& scissor = clip == PlotAnnotationClip2D::PlotArea ? frame.clip_rect() : frame.viewport();
            context.set_scissor(static_cast<int>(scissor.x()),
                                static_cast<int>(scissor.y()),
                                static_cast<int>(scissor.width()),
                                static_cast<int>(scissor.height()));
            tgfx::DrawList2DBuilder builder;
            const bool painted = bucket.scene.paint(builder, impl_->resources);
            const auto draw_list = painted ? builder.freeze() : std::nullopt;
            if (!draw_list || !impl_->canvas.execute(*draw_list, impl_->resources)) {
                tc::Log::error("PlotAnnotationLayer2D: DrawList2D execution failed");
            }
            impl_->canvas.end();
        }
    }

    void PlotAnnotationLayer2D::release_gpu_resources() {
        impl_->canvas.release_gpu();
    }

    const termin::visual::TcVisualScene& PlotAnnotationLayer2D::visual_scene(PlotAnnotationPhase2D phase,
                                                                             PlotAnnotationClip2D clip) const {
        return impl_->buckets[bucket_index(phase, clip)].scene;
    }

} // namespace tcplot
