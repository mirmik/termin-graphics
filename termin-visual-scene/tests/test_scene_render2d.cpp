#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <memory>
#include <variant>

#include <termin/geom/color.hpp>

#include "termin_visual_scene/builtin_items2d.hpp"
#include "termin_visual_scene/scene_render2d.hpp"

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
        bool fail_images = false;
        int calls = 0;

        std::optional<tgfx::FontHandle> resolve_font(std::string_view uri) override {
            assert(uri == "font://ui");
            ++calls;
            return tgfx::FontHandle{17};
        }

        std::optional<tgfx::TextureHandle> resolve_image(std::string_view uri) override {
            assert(uri == "image://logo");
            ++calls;
            if (fail_images)
                return std::nullopt;
            return tgfx::TextureHandle{23};
        }

        std::optional<ResolvedCustomBatch2D> resolve_custom_batch(std::string_view key,
                                                                  termin::Bounds2f bounds) override {
            assert(key == "batch://plot");
            assert(bounds.x0 == 0.0f && bounds.y0 == 0.0f);
            assert(bounds.x1 == 2.0f && bounds.y1 == 2.0f);
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

} // namespace

int main() {
    const auto scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};
    auto root_object = std::make_unique<GroupItem2D>();
    GroupItem2D* root_ptr = root_object.get();
    assert(scene.adopt(std::move(root_object)));
    root_ptr->set_local_transform(termin::Affine2f::translation(10.0f, 20.0f) * termin::Affine2f::shear(0.3f, 0.2f));
    root_ptr->set_opacity(0.5f);
    root_ptr->set_clip(GeometricClip2D{box(100.0f), tgfx::FillRule::NonZero});

    auto rect = std::make_unique<RectItem2D>(termin::Rect2f{0.0f, 0.0f, 10.0f, 12.0f}, tgfx::FillPaint{}, std::nullopt);
    rect->set_clip(GeometricClip2D{box(30.0f), tgfx::FillRule::EvenOdd});
    rect->set_z_order(7);
    assert(scene.adopt(std::move(rect), root_ptr));
    assert(scene.adopt(std::make_unique<PathItem2D>(box(8.0f), tgfx::FillPaint{}, std::nullopt), root_ptr));
    assert(scene.adopt(std::make_unique<TextItem2D>("direct",
                                                    "font://ui",
                                                    termin::Vec2f{1.0f, 2.0f},
                                                    13.0f,
                                                    termin::SrgbColor{},
                                                    tgfx::TextAnchor2D::Left,
                                                    termin::Bounds2f{1.0f, 2.0f, 40.0f, 15.0f},
                                                    1.2f),
                       root_ptr));
    assert(scene.adopt(std::make_unique<ImageItem2D>("image://logo",
                                                     termin::Rect2f{3.0f, 4.0f, 8.0f, 9.0f},
                                                     termin::Rect2f{0.0f, 0.0f, 1.0f, 1.0f},
                                                     termin::SrgbColor::white(),
                                                     tgfx::DrawTextureSampling2D::Linear),
                       root_ptr));
    assert(scene.adopt(std::make_unique<CustomBatchItem2D>("batch://plot", termin::Bounds2f{0.0f, 0.0f, 2.0f, 2.0f}),
                       root_ptr));

    Resolver resolver;
    tgfx::DrawList2DBuilder builder;
    assert(scene.paint(builder, resolver));
    const auto list = builder.freeze();
    assert(list);
    assert(resolver.calls == 3);

    bool saw_path = false;
    bool saw_text = false;
    bool saw_image = false;
    bool saw_custom = false;
    bool saw_nested_clips = false;
    int active_clips = 0;
    for (const auto& command : list->commands()) {
        if (std::holds_alternative<tgfx::PushClip2D>(command)) {
            ++active_clips;
            saw_nested_clips = saw_nested_clips || active_clips == 2;
        } else if (std::holds_alternative<tgfx::PopClip2D>(command)) {
            --active_clips;
        } else if (std::holds_alternative<tgfx::DrawPath2D>(command)) {
            saw_path = true;
        } else if (const auto* value = std::get_if<tgfx::DrawText2D>(&command)) {
            saw_text = value->font.id == 17 && value->coverage_gamma && *value->coverage_gamma == 1.2f;
        } else if (const auto* value = std::get_if<tgfx::DrawImage2D>(&command)) {
            saw_image = value->texture.id == 23;
        } else if (std::holds_alternative<tgfx::DrawCustomBatch2D>(command)) {
            saw_custom = true;
        }
    }
    assert(active_clips == 0);
    assert(saw_path && saw_text && saw_image && saw_custom && saw_nested_clips);

    const auto failing_scene_handle = tc_visual_scene_create();
    TcVisualScene failing_scene{failing_scene_handle};
    assert(failing_scene.adopt(std::make_unique<ImageItem2D>("image://logo",
                                                             termin::Rect2f{0.0f, 0.0f, 2.0f, 2.0f},
                                                             termin::Rect2f{0.0f, 0.0f, 1.0f, 1.0f},
                                                             termin::SrgbColor::white(),
                                                             tgfx::DrawTextureSampling2D::Linear)));
    resolver.fail_images = true;
    tgfx::DrawList2DBuilder failing_builder;
    assert(!failing_scene.paint(failing_builder, resolver));
    tc_visual_scene_destroy(failing_scene_handle);
    tc_visual_scene_destroy(scene_handle);
}
