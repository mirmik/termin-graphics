#ifdef NDEBUG
#undef NDEBUG
#endif

#include "tgfx2/draw_list2d.hpp"

#include <cassert>
#include <sstream>
#include <type_traits>

namespace {

tgfx::Path2f make_path() {
    tgfx::Path2f path;
    assert(path.move_to({0, 0}));
    assert(path.line_to({8, 0}));
    assert(path.line_to({4, 7}));
    assert(path.close());
    return path;
}

tgfx::DrawList2D make_list() {
    tgfx::DrawList2DBuilder builder;
    assert(builder.push_transform(
        termin::Affine2f::translation(10, 20) *
        termin::Affine2f::shear(0.25f, -0.1f)));
    assert(builder.push_opacity(0.75f));
    assert(builder.push_clip_rect({0, 0, 100, 80}));
    assert(builder.push_transform(termin::Affine2f::rotation(0.4f)));
    assert(builder.push_clip(make_path(), tgfx::FillRule::EvenOdd));

    tgfx::FillPaint fill{{0.2f, 0.3f, 0.4f, 1.0f}};
    tgfx::StrokePaint stroke;
    stroke.color = {0.9f, 0.8f, 0.7f, 0.6f};
    stroke.width = 2.0f;
    assert(builder.rect({1, 2, 30, 40}, fill));
    assert(builder.rounded_rect({3, 4, 20, 10}, 3, fill, stroke));
    assert(builder.ellipse({5, 6, 12, 16}, fill, stroke));
    assert(builder.path(make_path(), fill, stroke));
    const termin::Vec2f points[] = {{1, 1}, {2, 4}, {8, 3}};
    assert(builder.polyline(points, stroke, true));
    assert(builder.text(
        "frozen text", {7, 9}, 18, {1, 1, 1, 1},
        tgfx::FontHandle{17}, tgfx::TextAnchor2D::Center));
    assert(builder.image(
        tgfx::TextureHandle{23}, {9, 10, 40, 30}, {0.1f, 0.2f, 0.5f, 0.6f},
        {0.8f, 0.7f, 0.6f, 0.5f},
        tgfx::DrawTextureSampling2D::Nearest));
    const tgfx::DrawVertex2D triangle[] = {
        {{0, 0}, {0, 0}}, {{10, 0}, {1, 0}}, {{0, 10}, {0, 1}}};
    assert(builder.custom_batch(
        triangle, {1, 0, 0, 1}, tgfx::TextureHandle{29}));

    assert(builder.pop_clip());
    assert(builder.pop_transform());
    assert(builder.pop_clip());
    assert(builder.pop_opacity());
    assert(builder.pop_transform());
    auto frozen = builder.freeze();
    assert(frozen);

    // Reusing the builder cannot mutate data owned by the frozen list.
    builder.clear();
    assert(builder.rect({0, 0, 1, 1}, fill));
    auto unrelated = builder.freeze();
    assert(unrelated && unrelated->size() == 1);
    return std::move(*frozen);
}

std::string fingerprint(const tgfx::DrawList2D& list) {
    std::ostringstream out;
    for (const auto& command : list.commands()) {
        out << command.index() << ':';
        if (const auto* transform =
                std::get_if<tgfx::PushTransform2D>(&command)) {
            out << transform->transform.m00 << ','
                << transform->transform.m01 << ','
                << transform->transform.m10 << ','
                << transform->transform.m11 << ','
                << transform->transform.tx << ','
                << transform->transform.ty;
        } else if (const auto* clip =
                       std::get_if<tgfx::PushClip2D>(&command)) {
            out << clip->path.verbs().size() << ','
                << clip->path.points().size() << ','
                << static_cast<int>(clip->rule);
        } else if (const auto* text =
                       std::get_if<tgfx::DrawText2D>(&command)) {
            out << text->text << ',' << text->font.id;
        } else if (const auto* image =
                       std::get_if<tgfx::DrawImage2D>(&command)) {
            out << image->texture.id << ',' << image->rect.width;
        } else if (const auto* custom =
                       std::get_if<tgfx::DrawCustomBatch2D>(&command)) {
            out << custom->vertices.size() << ',' << custom->texture.id;
        }
        out << ';';
    }
    return out.str();
}

}  // namespace

int main() {
    static_assert(!std::is_pointer_v<decltype(tgfx::DrawText2D::font)>);
    static_assert(!std::is_pointer_v<decltype(tgfx::DrawImage2D::texture)>);

    const auto first = make_list();
    const auto second = make_list();
    assert(first.size() == 18);
    assert(fingerprint(first) == fingerprint(second));

    // A sheared transform remains exact and the nested clips remain paths;
    // neither is pre-collapsed to an axis-aligned device scissor.
    const auto* transform =
        std::get_if<tgfx::PushTransform2D>(&first.commands()[0]);
    assert(transform && transform->transform.m01 == 0.25f);
    const auto* outer_clip =
        std::get_if<tgfx::PushClip2D>(&first.commands()[2]);
    const auto* inner_clip =
        std::get_if<tgfx::PushClip2D>(&first.commands()[4]);
    assert(outer_clip && inner_clip);
    assert(outer_clip->path.points().size() == 4);
    assert(inner_clip->rule == tgfx::FillRule::EvenOdd);

    const auto* text =
        std::get_if<tgfx::DrawText2D>(&first.commands()[10]);
    const auto* image =
        std::get_if<tgfx::DrawImage2D>(&first.commands()[11]);
    const auto* custom =
        std::get_if<tgfx::DrawCustomBatch2D>(&first.commands()[12]);
    assert(text && text->text == "frozen text" && text->font.id == 17);
    assert(image && image->texture.id == 23);
    assert(custom && custom->vertices.size() == 3);

    // Appending a frozen list copies its immutable commands into the current
    // state scopes. This is how adapters apply camera transforms without
    // reopening or mutating the scene snapshot.
    tgfx::DrawList2DBuilder composed;
    assert(composed.push_transform(termin::Affine2f::translation(40, 50)));
    assert(composed.append(first));
    assert(composed.pop_transform());
    auto frozen_composed = composed.freeze();
    assert(frozen_composed);
    assert(frozen_composed->size() == first.size() + 2);
    assert(std::holds_alternative<tgfx::PushTransform2D>(
        frozen_composed->commands().front()));
    assert(std::holds_alternative<tgfx::PopTransform2D>(
        frozen_composed->commands().back()));
    assert(fingerprint(first) == fingerprint(second));

    tgfx::DrawList2DBuilder invalid;
    assert(invalid.push_opacity(0.5f));
    assert(!invalid.freeze());
    assert(!invalid.pop_clip());
    invalid.clear();
    assert(!invalid.text(
        "missing runtime font", {}, 12, {}, tgfx::FontHandle{}));
}
