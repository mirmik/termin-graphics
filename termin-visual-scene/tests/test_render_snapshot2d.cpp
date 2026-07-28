#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include <atomic>
#include <variant>

#include "termin_visual_scene/render_snapshot2d.hpp"

namespace {

using namespace termin::visual;

tgfx::Path2f box(float size) {
    tgfx::Path2f path;
    assert(path.move_to({0.0f, 0.0f}));
    assert(path.line_to({size, 0.0f}));
    assert(path.line_to({size, size}));
    assert(path.line_to({0.0f, size}));
    assert(path.close());
    return path;
}

class Resolver final : public SceneRenderResourceResolver2D {
public:
    VisualScene2D* scene = nullptr;
    GraphicItemHandle mutate = tc_graphic_item_handle_invalid();
    bool fail_images = false;
    std::atomic<int> calls{0};

    std::optional<tgfx::FontHandle> resolve_font(
        const StableResourceRef2D& reference) override {
        assert(reference.uri == "font://ui");
        ++calls;
        if (scene && !tc_graphic_item_handle_is_invalid(mutate)) {
            auto state = scene->snapshot(mutate)->state;
            state.local_transform = termin::Affine2f::translation(999.0f, 0.0f);
            assert(scene->set_state(mutate, state));
            mutate = tc_graphic_item_handle_invalid();
        }
        return tgfx::FontHandle{17};
    }

    std::optional<tgfx::TextureHandle> resolve_image(
        const StableResourceRef2D& reference) override {
        assert(reference.uri == "image://logo");
        ++calls;
        if (fail_images) return std::nullopt;
        return tgfx::TextureHandle{23};
    }

    std::optional<ResolvedCustomBatch2D> resolve_custom_batch(
        const CustomBatchItem2D& reference) override {
        assert(reference.key == "batch://plot");
        ++calls;
        return ResolvedCustomBatch2D{
            {
                {{0.0f, 0.0f}, {}},
                {{2.0f, 0.0f}, {}},
                {{0.0f, 2.0f}, {}},
            },
            {0.0f, 1.0f, 0.0f, 1.0f},
        };
    }
};

}  // namespace

int main() {
    VisualScene2D scene;
    const auto root = scene.create(GroupItem2D{});
    assert(root);
    GraphicItemState2D root_state;
    root_state.local_transform =
        termin::Affine2f::translation(10.0f, 20.0f) *
        termin::Affine2f::shear(0.3f, 0.2f);
    root_state.opacity = 0.5f;
    root_state.clip = GeometricClip2D{box(100.0f), tgfx::FillRule::NonZero};
    assert(scene.set_state(*root, root_state));

    const auto rect = scene.create(
        RectItem2D{{0.0f, 0.0f, 10.0f, 12.0f}, {}, std::nullopt},
        *root);
    const auto path = scene.create(
        PathItem2D{box(8.0f), tgfx::FillPaint{}, std::nullopt},
        *root);
    const auto text = scene.create(TextItem2D{
        "snapshot",
        {"font://ui"},
        {1.0f, 2.0f},
        13.0f,
        {},
        tgfx::TextAnchor2D::Left,
        {1.0f, 2.0f, 40.0f, 15.0f},
    }, *root);
    const auto image = scene.create(ImageItem2D{
        {"image://logo"}, {3.0f, 4.0f, 8.0f, 9.0f}}, *root);
    const auto custom = scene.create(CustomBatchItem2D{
        "batch://plot", {0.0f, 0.0f, 2.0f, 2.0f}}, *root);
    assert(rect && path && text && image && custom);

    GraphicItemState2D item_state;
    item_state.z_order = 7;
    item_state.local_transform =
        termin::Affine2f::rotation(0.25f) *
        termin::Affine2f::scaling(2.0f, 3.0f);
    item_state.clip = GeometricClip2D{box(30.0f), tgfx::FillRule::EvenOdd};
    assert(scene.set_state(*rect, item_state));
    item_state.z_order = 1;
    item_state.clip.reset();
    assert(scene.set_state(*path, item_state));
    item_state.z_order = 3;
    assert(scene.set_state(*text, item_state));
    item_state.z_order = 4;
    assert(scene.set_state(*image, item_state));
    item_state.z_order = 5;
    assert(scene.set_state(*custom, item_state));

    Resolver resolver;
    resolver.scene = &scene;
    resolver.mutate = *text;
    const auto revision_before = scene.revision();
    auto prepared = scene.prepare_render_snapshot(resolver);
    assert(prepared);
    assert(prepared->revision() == revision_before);
    assert(scene.revision() > prepared->revision());
    assert(prepared->items().size() == 6);
    assert(resolver.calls == 3);

    // Resolver mutation occurred after the locked value copy. The published
    // snapshot and frozen DrawList retain the previous exact transform.
    const auto& commands = prepared->draw_list().commands();
    bool saw_path = false;
    bool saw_text = false;
    bool saw_image = false;
    bool saw_custom = false;
    bool saw_nested_clips = false;
    int active_clips = 0;
    for (const auto& command : commands) {
        if (std::holds_alternative<tgfx::PushClip2D>(command)) {
            ++active_clips;
            saw_nested_clips = saw_nested_clips || active_clips == 2;
        } else if (std::holds_alternative<tgfx::PopClip2D>(command)) {
            --active_clips;
        } else if (std::holds_alternative<tgfx::DrawPath2D>(command)) {
            saw_path = true;
        } else if (const auto* value = std::get_if<tgfx::DrawText2D>(&command)) {
            saw_text = value->font.id == 17;
        } else if (const auto* value = std::get_if<tgfx::DrawImage2D>(&command)) {
            saw_image = value->texture.id == 23;
        } else if (std::holds_alternative<tgfx::DrawCustomBatch2D>(command)) {
            saw_custom = true;
        }
    }
    assert(active_clips == 0);
    assert(saw_path && saw_text && saw_image && saw_custom && saw_nested_clips);

    const auto preserved_size = prepared->draw_list().size();
    scene.clear();
    assert(scene.size() == 0);
    assert(prepared->items().size() == 6);
    assert(prepared->draw_list().size() == preserved_size);

    // Any unresolved resource aborts publication; no partial list escapes.
    VisualScene2D failing_scene;
    assert(failing_scene.create(ImageItem2D{
        {"image://logo"}, {0.0f, 0.0f, 2.0f, 2.0f}}));
    resolver.scene = nullptr;
    resolver.fail_images = true;
    assert(!failing_scene.prepare_render_snapshot(resolver));
}
