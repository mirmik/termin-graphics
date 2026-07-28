#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <optional>
#include <string>
#include <variant>

#include "termin_visual_scene/scene_inspection2d.hpp"

namespace {

using namespace termin::visual;

tgfx::Path2f triangle() {
    tgfx::Path2f result;
    assert(result.move_to({0.0f, 0.0f}));
    assert(result.line_to({8.0f, 0.0f}));
    assert(result.line_to({4.0f, 6.0f}));
    assert(result.close());
    return result;
}

void populate(VisualScene2D& scene) {
    const auto root = scene.create(GroupItem2D{});
    assert(root);
    GraphicItemState2D root_state;
    root_state.local_transform =
        termin::Affine2f::translation(12.0f, 18.0f)
        * termin::Affine2f::shear(0.25f, -0.1f);
    root_state.opacity = 0.75f;
    root_state.z_order = 4;
    root_state.clip = GeometricClip2D{
        triangle(), tgfx::FillRule::EvenOdd};
    assert(scene.set_state(*root, root_state));

    const tgfx::FillPaint fill{
        {0.1f, 0.2f, 0.3f, 0.8f},
        tgfx::FillRule::EvenOdd,
    };
    tgfx::StrokePaint stroke;
    stroke.color = {0.8f, 0.7f, 0.6f, 1.0f};
    stroke.width = 2.5f;
    stroke.join = tgfx::StrokeJoin::Round;
    stroke.cap = tgfx::StrokeCap::Square;
    stroke.miter_limit = 5.0f;
    stroke.dash_pattern = {2.0f, 3.0f};
    stroke.dash_offset = 0.5f;

    assert(scene.create(RectItem2D{
        {1.0f, 2.0f, 3.0f, 4.0f}, fill, stroke}, *root));
    assert(scene.create(RoundedRectItem2D{
        {2.0f, 3.0f, 5.0f, 6.0f}, 1.5f, fill, stroke}, *root));
    assert(scene.create(EllipseItem2D{
        {3.0f, 4.0f, 7.0f, 8.0f}, fill, stroke}, *root));
    assert(scene.create(PathItem2D{triangle(), fill, stroke}, *root));
    assert(scene.create(PolylineItem2D{
        {{1.0f, 2.0f}, {4.0f, 5.0f}, {8.0f, 3.0f}},
        stroke,
        true,
    }, *root));
    assert(scene.create(TextItem2D{
        "detached text",
        {"asset://font/main"},
        {5.0f, 7.0f},
        17.0f,
        {0.9f, 0.8f, 0.7f, 1.0f},
        tgfx::TextAnchor2D::Right,
        {5.0f, 7.0f, 90.0f, 24.0f},
    }, *root));
    assert(scene.create(ImageItem2D{
        {"asset://image/icon"},
        {6.0f, 8.0f, 20.0f, 18.0f},
        {0.1f, 0.2f, 0.7f, 0.6f},
        {0.8f, 0.9f, 1.0f, 0.75f},
        tgfx::DrawTextureSampling2D::Nearest,
    }, *root));
    assert(scene.create(HitRegionItem2D{
        triangle(), tgfx::FillRule::EvenOdd}, *root));
    assert(scene.create(CustomBatchItem2D{
        "plot:series-main", {-4.0f, -3.0f, 12.0f, 14.0f}}, *root));
}

}  // namespace

int main() {
    SceneInspection2D detached;
    tc::trent serialized;
    {
        VisualScene2D source;
        populate(source);
        detached = source.inspection();
        serialized = source.serialize();
        assert(detached.items.size() == 10);
        assert(detached.items[0].type_name == "termin.visual.Group2D");
        assert(!detached.items[0].parent_index);
        assert(detached.items[0].children.size() == 9);
        for (std::uint32_t i = 1; i < detached.items.size(); ++i) {
            assert(detached.items[i].record_index == i);
            assert(detached.items[i].parent_index == 0);
            assert(detached.items[i].stable_id != 0);
            assert(detached.items[i].depth == 1);
        }
        assert(serialized["schema"].as_string()
               == "termin.visual_scene.2d");
        assert(serialized["version"].as_integer() == 1);
        assert(!serialized["items"][0].contains("hovered"));
        assert(!serialized["items"][0].contains("pressed"));
        assert(!serialized["items"][0].contains("captured"));
        assert(!serialized["items"][0].contains("dirty"));
    }

    // Inspection is a deep detached value after scene destruction.
    assert(detached.items.size() == 10);
    assert(std::get<TextItem2D>(detached.items[6].payload).text
           == "detached text");
    assert(detached.items[0].local_bounds);

    VisualScene2D restored;
    assert(restored.restore(serialized));
    const auto round_trip = restored.inspection();
    assert(round_trip.scene_revision == detached.scene_revision);
    assert(round_trip.items.size() == detached.items.size());
    for (std::size_t i = 0; i < round_trip.items.size(); ++i) {
        assert(round_trip.items[i].record_index
               == detached.items[i].record_index);
        assert(round_trip.items[i].parent_index
               == detached.items[i].parent_index);
        assert(round_trip.items[i].children == detached.items[i].children);
        assert(round_trip.items[i].type_name
               == detached.items[i].type_name);
        assert(round_trip.items[i].stable_id
               == detached.items[i].stable_id);
        assert(round_trip.items[i].revision
               == detached.items[i].revision);
    }
    assert(std::holds_alternative<RectItem2D>(
        round_trip.items[1].payload));
    assert(std::holds_alternative<RoundedRectItem2D>(
        round_trip.items[2].payload));
    assert(std::holds_alternative<EllipseItem2D>(
        round_trip.items[3].payload));
    assert(std::holds_alternative<PathItem2D>(
        round_trip.items[4].payload));
    assert(std::holds_alternative<PolylineItem2D>(
        round_trip.items[5].payload));
    assert(std::holds_alternative<TextItem2D>(
        round_trip.items[6].payload));
    assert(std::holds_alternative<ImageItem2D>(
        round_trip.items[7].payload));
    assert(std::holds_alternative<HitRegionItem2D>(
        round_trip.items[8].payload));
    assert(std::holds_alternative<CustomBatchItem2D>(
        round_trip.items[9].payload));

    // Restore is intentionally empty-scene-only.
    assert(!restored.restore(serialized));
    assert(restored.size() == 10);

    tc::trent unknown_type = serialized;
    unknown_type["items"][1]["type"] = "example.UnknownItem";
    VisualScene2D unknown_destination;
    assert(!unknown_destination.restore(unknown_type));
    assert(unknown_destination.size() == 0);

    tc::trent invalid_topology = serialized;
    invalid_topology["items"][1]["parent"] = nullptr;
    VisualScene2D topology_destination;
    assert(!topology_destination.restore(invalid_topology));
    assert(topology_destination.size() == 0);

    tc::trent invalid_payload = serialized;
    invalid_payload["items"][1]["payload"]["rect"][2] = -1.0f;
    VisualScene2D payload_destination;
    assert(!payload_destination.restore(invalid_payload));
    assert(payload_destination.size() == 0);

    tc::trent invalid_version = serialized;
    invalid_version["version"] = 2;
    VisualScene2D version_destination;
    assert(!version_destination.restore(invalid_version));
    assert(version_destination.size() == 0);

    return 0;
}
