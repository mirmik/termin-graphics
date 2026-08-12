#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "termin_visual_scene/paint3d.hpp"
#include "termin_visual_scene/visual_item3d.hpp"

namespace {

    struct PublishedPacket {
        std::string protocol;
        int value = 0;
        tc_visual_item3d_handle item = tc_visual_item3d_handle_invalid();
        termin::Affine3d world_from_local = termin::Affine3d::identity();
        bool enabled = false;
        const void* view_extension = nullptr;
    };

    class RecordingSink final : public termin::visual::ScenePaintSink3D {
    public:
        bool begin(const termin::visual::VisualView3D& view) override {
            assert(!active);
            active = true;
            ++begins;
            observed_view = &view;
            staged.clear();
            return begin_result;
        }

        bool submit(const termin::visual::DrawSubmission3D& submission) override {
            assert(active);
            ++submits;
            if (!submit_result)
                return false;
            assert(submission.packet.payload_size == sizeof(int));
            int value = 0;
            std::memcpy(&value, submission.packet.payload, sizeof(value));
            staged.push_back({submission.packet.protocol,
                              value,
                              submission.item,
                              submission.world_from_local,
                              submission.effective_enabled,
                              submission.view->extension});
            return true;
        }

        bool end() override {
            assert(active);
            ++ends;
            if (!end_result)
                return false;
            published = staged;
            staged.clear();
            active = false;
            return true;
        }

        void abort() override {
            ++aborts;
            staged.clear();
            active = false;
        }

        bool begin_result = true;
        bool submit_result = true;
        bool end_result = true;
        bool active = false;
        int begins = 0;
        int submits = 0;
        int ends = 0;
        int aborts = 0;
        const termin::visual::VisualView3D* observed_view = nullptr;
        std::vector<PublishedPacket> staged;
        std::vector<PublishedPacket> published;
    };

    class ScalarItem final : public termin::visual::VisualItem3D {
    public:
        explicit ScalarItem(int value)
            : VisualItem3D(&VTABLE, "termin.visual.test.ScalarItem3D"),
              value(value) {}

        int value = 0;
        bool paint_result = true;
        mutable int paint_calls = 0;

    private:
        static bool paint_item(const tc_visual_item3d* item, tc_visual_item_paint_context3d* context) {
            const auto* self = static_cast<const ScalarItem*>(item->body);
            ++self->paint_calls;
            if (!self->paint_result)
                return false;
            int borrowed_payload = self->value;
            return tc_visual_item_paint_context3d_submit(
                context, "termin.visual.test.scalar", &borrowed_payload, sizeof(borrowed_payload));
        }

        static const tc_visual_item3d_vtable VTABLE;
    };

    const tc_visual_item3d_vtable ScalarItem::VTABLE{
        .type_name = "termin.visual.test.ScalarItem3D",
        .paint = ScalarItem::paint_item,
    };

    class MarkerItem final : public termin::visual::VisualItem3D {
    public:
        explicit MarkerItem(int marker)
            : VisualItem3D(&VTABLE, "termin.visual.test.MarkerItem3D"),
              marker(marker) {}

        int marker = 0;
        mutable int paint_calls = 0;

    private:
        static bool paint_item(const tc_visual_item3d* item, tc_visual_item_paint_context3d* raw_context) {
            const auto* self = static_cast<const MarkerItem*>(item->body);
            ++self->paint_calls;
            termin::visual::GraphicItemPaintContext3D context(raw_context);
            assert(context.effective_visible());
            assert(context.item().scene_id == item->handle.scene_id);
            const int borrowed_payload = self->marker;
            return context.submit("termin.visual.test.marker", &borrowed_payload, sizeof(borrowed_payload));
        }

        static const tc_visual_item3d_vtable VTABLE;
    };

    const tc_visual_item3d_vtable MarkerItem::VTABLE{
        .type_name = "termin.visual.test.MarkerItem3D",
        .paint = MarkerItem::paint_item,
    };

    tc_mat44 identity_matrix() {
        tc_mat44 result{};
        result.m[0] = 1.0;
        result.m[5] = 1.0;
        result.m[10] = 1.0;
        result.m[15] = 1.0;
        return result;
    }

} // namespace

int main() {
    using termin::Affine3d;
    using termin::visual::TcVisualScene3D;
    using termin::visual::VisualView3D;

    const auto scene_handle = tc_visual_scene3d_create();
    TcVisualScene3D scene(scene_handle);

    auto scalar = std::make_unique<ScalarItem>(10);
    auto* scalar_ptr = scalar.get();
    scalar_ptr->set_local_transform(Affine3d::from_translation(1.0, 2.0, 3.0));
    const auto scalar_handle = scene.adopt(std::move(scalar));
    assert(scalar_handle);

    auto marker = std::make_unique<MarkerItem>(20);
    auto* marker_ptr = marker.get();
    marker_ptr->set_local_transform(Affine3d::from_translation(4.0, 0.0, 0.0));
    const auto marker_handle = scene.adopt(std::move(marker), scalar_ptr);
    assert(marker_handle);

    int extension_token = 42;
    VisualView3D view{
        .view_matrix = identity_matrix(),
        .projection_matrix = identity_matrix(),
        .camera_world_position = {7.0, 8.0, 9.0},
        .viewport_width = 640,
        .viewport_height = 480,
        .extension = &extension_token,
    };
    RecordingSink sink;
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.begins == 1 && sink.ends == 1 && sink.aborts == 0);
    assert(sink.observed_view == &view);
    assert(sink.published.size() == 2);
    assert(sink.published[0].protocol == "termin.visual.test.scalar");
    assert(sink.published[0].value == 10);
    assert(sink.published[0].world_from_local.translation.x == 1.0);
    assert(sink.published[1].protocol == "termin.visual.test.marker");
    assert(sink.published[1].value == 20);
    assert(sink.published[1].world_from_local.translation.x == 5.0);
    assert(sink.published[1].world_from_local.translation.y == 2.0);
    assert(sink.published[1].view_extension == &extension_token);

    // The sink copied borrowed payloads synchronously; later item mutation does
    // not change the already published batch.
    scalar_ptr->value = 99;
    marker_ptr->marker = 88;
    assert(sink.published[0].value == 10 && sink.published[1].value == 20);

    scalar_ptr->set_enabled(false);
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.published.size() == 2);
    assert(!sink.published[0].enabled && !sink.published[1].enabled);
    scalar_ptr->set_enabled(true);

    scalar_ptr->set_visible(false);
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.published.empty());
    assert(scalar_ptr->paint_calls == 2);
    assert(marker_ptr->paint_calls == 2);
    scalar_ptr->set_visible(true);

    auto replacement = std::make_unique<MarkerItem>(30);
    auto* replacement_ptr = replacement.get();
    assert(scene.replace(*marker_handle, std::move(replacement)));
    assert(termin::visual::paint(scene, view, sink));
    assert(sink.published.size() == 2);
    assert(sink.published[1].value == 30);
    assert(sink.published[1].world_from_local.translation.x == 5.0);

    // A partially staged frame is discarded when an item reports failure.
    scalar_ptr->paint_result = false;
    const auto previous_published = sink.published;
    assert(!termin::visual::paint(scene, view, sink));
    assert(sink.aborts == 1);
    assert(sink.published.size() == previous_published.size());
    assert(sink.published[1].value == previous_published[1].value);
    scalar_ptr->paint_result = true;

    sink.submit_result = false;
    assert(!termin::visual::paint(scene, view, sink));
    assert(sink.aborts == 2);
    sink.submit_result = true;

    sink.end_result = false;
    assert(!termin::visual::paint(scene, view, sink));
    assert(sink.aborts == 3);
    sink.end_result = true;

    // Invalid caller-owned view data is rejected before a batch starts.
    view.viewport_width = 0;
    assert(!termin::visual::paint(scene, view, sink));
    view.viewport_width = 640;

    assert(replacement_ptr->paint_calls == 2);
    scene.clear();
    tc_visual_scene3d_destroy(scene_handle);
}
