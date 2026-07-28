#include "tcplot/plot_annotations2d.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

#include <tcbase/tc_log.hpp>
#include <tgfx2/canvas2d_renderer.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/render_context.hpp>
#include <termin/geom/affine2.hpp>
#include <termin_visual_scene/render_snapshot2d.hpp>

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

std::size_t bucket_index(
    PlotAnnotationPhase2D phase,
    PlotAnnotationClip2D clip) {
    return static_cast<std::size_t>(phase) * kClipCount
        + static_cast<std::size_t>(clip);
}

bool same_graphic_handle(GraphicItemHandle lhs, GraphicItemHandle rhs) {
    return lhs.scene_id == rhs.scene_id
        && lhs.index == rhs.index
        && lhs.generation == rhs.generation;
}

bool invalid_graphic_handle(GraphicItemHandle value) {
    return tc_graphic_item_handle_is_invalid(value);
}

bool finite(PlotPixelPoint2D point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

std::optional<PlotPixelPoint2D> resolve_anchor(
    const PlotAnchor2D& anchor,
    const PlotFrame2D& frame,
    const PlotData& data) {
    return std::visit(
        [&](const auto& value) -> std::optional<PlotPixelPoint2D> {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, DataAnchor2D>) {
                const auto result = frame.data_to_pixel(value.x, value.y);
                return finite(result)
                    ? std::optional<PlotPixelPoint2D>(result)
                    : std::nullopt;
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
                if (value.point_index >= x->size()
                    || value.point_index >= y->size()) {
                    return std::nullopt;
                }
                const auto result =
                    frame.data_to_pixel((*x)[value.point_index],
                                        (*y)[value.point_index]);
                return finite(result)
                    ? std::optional<PlotPixelPoint2D>(result)
                    : std::nullopt;
            } else if constexpr (std::is_same_v<T, AxesFractionAnchor2D>) {
                const PlotRect2D& area = frame.plot_area();
                const PlotPixelPoint2D result{
                    area.x()
                        + static_cast<float>(value.x) * area.width(),
                    area.bottom()
                        - static_cast<float>(value.y) * area.height(),
                };
                return finite(result)
                    ? std::optional<PlotPixelPoint2D>(result)
                    : std::nullopt;
            } else {
                const PlotPixelPoint2D result{
                    frame.viewport().x() + value.x,
                    frame.viewport().y() + value.y,
                };
                return finite(result)
                    ? std::optional<PlotPixelPoint2D>(result)
                    : std::nullopt;
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
            throw std::logic_error(
                "render phase does not contain plot annotations");
    }
}

}  // namespace

struct PlotAnnotationLayer2D::Impl {
    struct Projection {
        GraphicItemHandle item = tc_graphic_item_handle_invalid();
        std::size_t visual_index = 0;
        std::size_t bucket = 0;
    };

    struct Record {
        std::uint32_t generation = 1;
        bool alive = false;
        PlotAnnotation2D annotation;
        std::optional<PlotPixelPoint2D> projected_anchor;
        std::vector<Projection> projections;
        SnapHook snap_hook;
        ActionHandler action_handler;
    };

    class Resources final
        : public termin::visual::SceneRenderResourceResolver2D,
          public tgfx::DrawResourceResolver2D {
    public:
        tgfx::FontAtlas* font = nullptr;

        std::optional<tgfx::FontHandle> resolve_font(
            const termin::visual::StableResourceRef2D& reference) override {
            if (reference.uri != kPlotDefaultFontResource2D || !font) {
                tc::Log::error(
                    "PlotAnnotationLayer2D: unresolved font resource '%s'",
                    reference.uri.c_str());
                return std::nullopt;
            }
            return tgfx::FontHandle{1};
        }

        std::optional<tgfx::TextureHandle> resolve_image(
            const termin::visual::StableResourceRef2D& reference) override {
            tc::Log::error(
                "PlotAnnotationLayer2D: unresolved image resource '%s'",
                reference.uri.c_str());
            return std::nullopt;
        }

        std::optional<termin::visual::ResolvedCustomBatch2D>
        resolve_custom_batch(
            const termin::visual::CustomBatchItem2D& reference) override {
            tc::Log::error(
                "PlotAnnotationLayer2D: unresolved custom batch '%s'",
                reference.key.c_str());
            return std::nullopt;
        }

        tgfx::FontAtlas* resolve_font(tgfx::FontHandle handle) override {
            return handle.id == 1 ? font : nullptr;
        }
    };

    struct Bucket {
        termin::visual::VisualScene2D scene;
        termin::visual::SceneInteraction2D interaction;
    };

    std::uint64_t id = g_next_annotation_layer_id.fetch_add(1);
    std::vector<Record> records;
    std::vector<std::uint32_t> free_indices;
    std::array<Bucket, kBucketCount> buckets;
    tgfx::Canvas2DRenderer canvas;
    Resources resources;

    Record* resolve(PlotAnnotationHandle handle) {
        if (handle.layer_id != id || handle.index >= records.size()) {
            return nullptr;
        }
        Record& record = records[handle.index];
        return record.alive && record.generation == handle.generation
            ? &record
            : nullptr;
    }

    const Record* resolve(PlotAnnotationHandle handle) const {
        if (handle.layer_id != id || handle.index >= records.size()) {
            return nullptr;
        }
        const Record& record = records[handle.index];
        return record.alive && record.generation == handle.generation
            ? &record
            : nullptr;
    }

    PlotAnnotationHandle handle(std::uint32_t index) const {
        return {id, index, records[index].generation};
    }

    void clear_projection(Record& record) {
        for (const Projection& projection : record.projections) {
            Bucket& bucket = buckets[projection.bucket];
            bucket.interaction.cancel_all();
            bucket.interaction.clear_action_handler(projection.item);
            if (!bucket.scene.destroy_leaf(projection.item)) {
                tc::Log::error(
                    "PlotAnnotationLayer2D: failed to destroy projected item");
            }
        }
        record.projections.clear();
        record.projected_anchor.reset();
    }

    void bind_action(
        std::uint32_t record_index,
        const Projection& projection,
        const PlotAnnotationVisual2D& visual) {
        buckets[projection.bucket].interaction.set_action_handler(
            projection.item,
            [this,
             record_index,
             generation = records[record_index].generation,
             visual_index = projection.visual_index,
             action = visual.action](
                const termin::visual::ActionEvent2D& event) {
                const PlotAnnotationHandle annotation{
                    id, record_index, generation};
                Record* record = resolve(annotation);
                if (!record || !record->action_handler) return;
                try {
                    record->action_handler({
                        annotation,
                        visual_index,
                        event.pointer,
                        action,
                    });
                } catch (const std::exception& error) {
                    tc::Log::error(
                        "PlotAnnotationLayer2D: action handler failed: %s",
                        error.what());
                    throw;
                } catch (...) {
                    tc::Log::error(
                        "PlotAnnotationLayer2D: action handler failed");
                    throw;
                }
            });
    }

    bool projection_topology_matches(const Record& record) const {
        if (record.projections.size() != record.annotation.visuals.size()) {
            return false;
        }
        for (std::size_t i = 0; i < record.projections.size(); ++i) {
            const auto& visual = record.annotation.visuals[i];
            if (record.projections[i].visual_index != i
                || record.projections[i].bucket
                    != bucket_index(visual.phase, visual.clip)) {
                return false;
            }
        }
        return true;
    }

    void project_record(
        std::uint32_t record_index,
        const PlotFrame2D& frame,
        const PlotData& data) {
        Record& record = records[record_index];
        const auto anchor =
            resolve_anchor(record.annotation.anchor, frame, data);
        if (!anchor) {
            clear_projection(record);
            return;
        }

        if (!projection_topology_matches(record)) {
            clear_projection(record);
            record.projections.reserve(record.annotation.visuals.size());
            for (std::size_t i = 0;
                 i < record.annotation.visuals.size();
                 ++i) {
                const PlotAnnotationVisual2D& visual =
                    record.annotation.visuals[i];
                const std::size_t bucket =
                    bucket_index(visual.phase, visual.clip);
                const auto item =
                    buckets[bucket].scene.create(visual.payload);
                if (!item) {
                    tc::Log::error(
                        "PlotAnnotationLayer2D: failed to create projected item");
                    clear_projection(record);
                    return;
                }
                record.projections.push_back({*item, i, bucket});
                bind_action(
                    record_index, record.projections.back(), visual);
            }
        }

        for (const Projection& projection : record.projections) {
            const PlotAnnotationVisual2D& visual =
                record.annotation.visuals[projection.visual_index];
            termin::visual::GraphicItemState2D state;
            state.local_transform = termin::Affine2f::translation(
                anchor->x + visual.pixel_offset.x,
                anchor->y + visual.pixel_offset.y);
            state.z_order = visual.z_order;
            state.visible = visual.visible;
            state.enabled = visual.enabled;
            Bucket& bucket = buckets[projection.bucket];
            if (!bucket.scene.set_item(
                    projection.item, state, visual.payload)) {
                tc::Log::error(
                    "PlotAnnotationLayer2D: failed to update projected item");
                clear_projection(record);
                return;
            }
        }
        record.projected_anchor = anchor;
    }

    std::vector<std::size_t> front_to_back_buckets() const {
        return {
            bucket_index(
                PlotAnnotationPhase2D::Chrome,
                PlotAnnotationClip2D::Viewport),
            bucket_index(
                PlotAnnotationPhase2D::Chrome,
                PlotAnnotationClip2D::PlotArea),
            bucket_index(
                PlotAnnotationPhase2D::Overlay,
                PlotAnnotationClip2D::Viewport),
            bucket_index(
                PlotAnnotationPhase2D::Overlay,
                PlotAnnotationClip2D::PlotArea),
            bucket_index(
                PlotAnnotationPhase2D::Underlay,
                PlotAnnotationClip2D::Viewport),
            bucket_index(
                PlotAnnotationPhase2D::Underlay,
                PlotAnnotationClip2D::PlotArea),
        };
    }

    bool bucket_is_plot_clipped(std::size_t bucket) const {
        return bucket % kClipCount
            == static_cast<std::size_t>(PlotAnnotationClip2D::PlotArea);
    }
};

PlotAnnotationLayer2D::PlotAnnotationLayer2D()
    : impl_(std::make_unique<Impl>()) {}

PlotAnnotationLayer2D::~PlotAnnotationLayer2D() = default;

std::optional<PlotAnnotationHandle> PlotAnnotationLayer2D::create(
    PlotAnnotation2D annotation) {
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
    return impl_->handle(index);
}

bool PlotAnnotationLayer2D::update(
    PlotAnnotationHandle handle,
    PlotAnnotation2D annotation) {
    Impl::Record* record = impl_->resolve(handle);
    if (!record) return false;
    impl_->clear_projection(*record);
    record->annotation = std::move(annotation);
    return true;
}

bool PlotAnnotationLayer2D::destroy(PlotAnnotationHandle handle) {
    Impl::Record* record = impl_->resolve(handle);
    if (!record) return false;
    impl_->clear_projection(*record);
    record->alive = false;
    record->annotation = {};
    record->snap_hook = {};
    record->action_handler = {};
    ++record->generation;
    if (record->generation == 0) ++record->generation;
    impl_->free_indices.push_back(handle.index);
    return true;
}

void PlotAnnotationLayer2D::clear() {
    for (std::uint32_t i = 0; i < impl_->records.size(); ++i) {
        if (!impl_->records[i].alive) continue;
        destroy(impl_->handle(i));
    }
}

std::optional<PlotAnnotationSnapshot2D> PlotAnnotationLayer2D::snapshot(
    PlotAnnotationHandle handle) const {
    const Impl::Record* record = impl_->resolve(handle);
    if (!record) return std::nullopt;
    PlotAnnotationSnapshot2D result;
    result.handle = handle;
    result.annotation = record->annotation;
    result.projected_anchor = record->projected_anchor;
    result.projected_graphics.reserve(record->projections.size());
    for (const Impl::Projection& projection : record->projections) {
        const auto& visual =
            record->annotation.visuals[projection.visual_index];
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
        if (!impl_->records[i].alive) continue;
        result.push_back(*snapshot(impl_->handle(i)));
    }
    return result;
}

std::size_t PlotAnnotationLayer2D::size() const {
    return static_cast<std::size_t>(std::count_if(
        impl_->records.begin(),
        impl_->records.end(),
        [](const Impl::Record& record) { return record.alive; }));
}

bool PlotAnnotationLayer2D::set_snap_hook(
    PlotAnnotationHandle handle,
    SnapHook hook) {
    Impl::Record* record = impl_->resolve(handle);
    if (!record) return false;
    record->snap_hook = std::move(hook);
    return true;
}

std::optional<PlotPoint2D> PlotAnnotationLayer2D::snap_data(
    PlotAnnotationHandle handle,
    PlotPoint2D candidate) const {
    const Impl::Record* record = impl_->resolve(handle);
    if (!record) return std::nullopt;
    if (!record->snap_hook) return candidate;
    try {
        return record->snap_hook(candidate);
    } catch (const std::exception& error) {
        tc::Log::error(
            "PlotAnnotationLayer2D: snap hook failed: %s",
            error.what());
        throw;
    } catch (...) {
        tc::Log::error("PlotAnnotationLayer2D: snap hook failed");
        throw;
    }
}

bool PlotAnnotationLayer2D::set_action_handler(
    PlotAnnotationHandle handle,
    ActionHandler handler) {
    Impl::Record* record = impl_->resolve(handle);
    if (!record) return false;
    record->action_handler = std::move(handler);
    return true;
}

void PlotAnnotationLayer2D::project(
    const PlotFrame2D& frame,
    const PlotData& data) {
    for (std::uint32_t i = 0; i < impl_->records.size(); ++i) {
        if (!impl_->records[i].alive) continue;
        impl_->project_record(i, frame, data);
    }
}

bool PlotAnnotationLayer2D::route_pointer(
    const PlotFrame2D& frame,
    const PointerEvent2D& event) {
    const auto order = impl_->front_to_back_buckets();

    // Captured interaction owns move/up even after the pointer leaves its
    // clipping region.
    for (const std::size_t bucket : order) {
        if (!invalid_graphic_handle(
                impl_->buckets[bucket].interaction.captured(event.pointer))) {
            const PointerDispatch2D dispatch =
                impl_->buckets[bucket].interaction.route(
                    impl_->buckets[bucket].scene, event);
            return !invalid_graphic_handle(dispatch.target);
        }
    }

    for (const std::size_t bucket : order) {
        if (impl_->bucket_is_plot_clipped(bucket)
            && !frame.contains_plot_pixel(
                event.position.x, event.position.y)) {
            continue;
        }
        const PointerDispatch2D dispatch =
            impl_->buckets[bucket].interaction.route(
                impl_->buckets[bucket].scene, event);
        if (!invalid_graphic_handle(dispatch.target)) return true;
    }
    return false;
}

bool PlotAnnotationLayer2D::hit_test(
    const PlotFrame2D& frame,
    float x,
    float y) const {
    for (const std::size_t bucket : impl_->front_to_back_buckets()) {
        if (impl_->bucket_is_plot_clipped(bucket)
            && !frame.contains_plot_pixel(x, y)) {
            continue;
        }
        if (termin::visual::hit_test(
                impl_->buckets[bucket].scene, {x, y})) {
            return true;
        }
    }
    return false;
}

void PlotAnnotationLayer2D::render_phase(
    PlotRenderPhase2D phase,
    const PlotFrame2D& frame,
    tgfx::RenderContext2& context,
    tgfx::FontAtlas* font) {
    if (phase != PlotRenderPhase2D::AnnotationUnderlay
        && phase != PlotRenderPhase2D::AnnotationOverlay
        && phase != PlotRenderPhase2D::UnclippedChrome) {
        return;
    }
    const PlotAnnotationPhase2D annotation = annotation_phase(phase);
    impl_->resources.font = font;

    for (const PlotAnnotationClip2D clip : {
             PlotAnnotationClip2D::PlotArea,
             PlotAnnotationClip2D::Viewport}) {
        Impl::Bucket& bucket =
            impl_->buckets[bucket_index(annotation, clip)];
        if (bucket.scene.size() == 0) continue;
        const auto prepared =
            bucket.scene.prepare_render_snapshot(impl_->resources);
        if (!prepared) {
            tc::Log::error(
                "PlotAnnotationLayer2D: failed to prepare render snapshot");
            continue;
        }

        const PlotRect2D& viewport = frame.viewport();
        impl_->canvas.set_default_font(font);
        impl_->canvas.begin(
            context,
            static_cast<int>(viewport.x()),
            static_cast<int>(viewport.y()),
            static_cast<int>(viewport.width()),
            static_cast<int>(viewport.height()));
        const PlotRect2D& scissor =
            clip == PlotAnnotationClip2D::PlotArea
                ? frame.clip_rect()
                : frame.viewport();
        context.set_scissor(
            static_cast<int>(scissor.x()),
            static_cast<int>(scissor.y()),
            static_cast<int>(scissor.width()),
            static_cast<int>(scissor.height()));
        if (!impl_->canvas.execute(
                prepared->draw_list(), impl_->resources)) {
            tc::Log::error(
                "PlotAnnotationLayer2D: DrawList2D execution failed");
        }
        impl_->canvas.end();
    }
}

void PlotAnnotationLayer2D::release_gpu_resources() {
    impl_->canvas.release_gpu();
}

const termin::visual::VisualScene2D& PlotAnnotationLayer2D::visual_scene(
    PlotAnnotationPhase2D phase,
    PlotAnnotationClip2D clip) const {
    return impl_->buckets[bucket_index(phase, clip)].scene;
}

}  // namespace tcplot
