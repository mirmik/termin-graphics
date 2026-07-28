#include "visual_scene_composition.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <tcbase/tc_log.h>
#include <termin_visual_scene/interaction2d.hpp>

namespace termin::gui_native::examples {
namespace {

tgfx::FillPaint fill(float r, float g, float b, float a = 1.0f) {
    return {{r, g, b, a}};
}

tgfx::StrokePaint stroke(float r, float g, float b, float width) {
    return {{r, g, b, 1.0f}, width};
}

tgfx::Path2f rectangle_path(float width, float height) {
    tgfx::Path2f path;
    if (!path.move_to({0.0f, 0.0f}) ||
        !path.line_to({width, 0.0f}) ||
        !path.line_to({width, height}) ||
        !path.line_to({0.0f, height}) ||
        !path.close()) {
        throw std::runtime_error("failed to build portal hit path");
    }
    return path;
}

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

bool same_widget(tc_widget_handle left, tc_widget_handle right) {
    return tc_widget_handle_eq(left, right);
}

bool near(float left, float right) {
    return std::fabs(left - right) < 0.001f;
}

template <typename Item, typename... Args>
Item* adopt(
    termin::visual::TcVisualScene& scene,
    termin::visual::GraphicItem2D* parent,
    Args&&... args) {
    auto item = std::make_unique<Item>(
        std::forward<Args>(args)...);
    auto* result = item.get();
    require(
        scene.adopt(std::move(item), parent).has_value(),
        "failed to adopt visual item");
    return result;
}

tc_ui_point position(
    const termin::visual::GraphicItem2D& item) {
    const auto& transform = item.local_transform();
    return {transform.tx, transform.ty};
}

void set_position(
    termin::visual::GraphicItem2D& item,
    tc_ui_point value) {
    auto transform = item.local_transform();
    transform.tx = value.x;
    transform.ty = value.y;
    item.set_local_transform(transform);
}

bool measure_headless_text(
    void*,
    const char* text,
    std::size_t byte_length,
    float font_size,
    tc_ui_text_metrics* out) {
    if (!text || !out || !std::isfinite(font_size) || font_size <= 0.0f) {
        return false;
    }
    float width = 0.0f;
    std::size_t offset = 0;
    while (offset < byte_length) {
        const auto first = static_cast<std::uint8_t>(text[offset]);
        std::size_t codepoint_bytes = 0;
        if (first < 0x80u) {
            codepoint_bytes = 1;
            width += font_size * 0.52f;
        } else if ((first & 0xe0u) == 0xc0u) {
            codepoint_bytes = 2;
            width += font_size * 0.62f;
        } else if ((first & 0xf0u) == 0xe0u) {
            codepoint_bytes = 3;
            width += font_size * 0.72f;
        } else if ((first & 0xf8u) == 0xf0u) {
            codepoint_bytes = 4;
            width += font_size;
        } else {
            return false;
        }
        if (offset + codepoint_bytes > byte_length) return false;
        for (std::size_t index = 1; index < codepoint_bytes; ++index) {
            if ((static_cast<std::uint8_t>(text[offset + index]) & 0xc0u) !=
                0x80u) {
                return false;
            }
        }
        offset += codepoint_bytes;
    }
    out->width = width;
    out->height = font_size;
    out->ascent = font_size * 0.8f;
    out->descent = font_size * 0.2f;
    out->line_height = font_size * 1.2f;
    return true;
}

void click(TcDocument document, tc_ui_point point) {
    require(
        document.dispatch_pointer_event(
            {TC_UI_POINTER_DOWN, point.x, point.y, 0, 1, 0, 0.0f, 0.0f}) ==
            TC_UI_EVENT_HANDLED,
        "pointer down was not handled");
    require(
        document.dispatch_pointer_event(
            {TC_UI_POINTER_UP, point.x, point.y, 0, 1, 0, 0.0f, 0.0f}) ==
            TC_UI_EVENT_HANDLED,
        "pointer up was not handled");
}

std::size_t first_command(
    const tc_ui_draw_list* draw_list,
    tc_ui_draw_command_type type) {
    for (std::size_t index = 0;
         index < tc_ui_draw_list_command_count(draw_list);
         ++index) {
        const auto* command =
            tc_ui_draw_list_command_at(draw_list, index);
        if (command && command->type == type) return index;
    }
    return tc_ui_draw_list_command_count(draw_list);
}

} // namespace

VisualSceneCompositionRefs build_visual_scene_composition(
    TcDocument document) {
    if (!document.valid()) {
        throw std::invalid_argument(
            "visual scene composition requires a live document");
    }

    DocumentBuilder ui(document);
    VisualSceneCompositionRefs refs;
    refs.state = std::make_shared<VisualSceneCompositionState>();
    refs.state->scene = tc_visual_scene_create();
    refs.scene =
        termin::visual::TcVisualScene{refs.state->scene};

    refs.root = &ui.make_root<BoxLayout>(
        Orientation::Horizontal, "VisualSceneCompositionRoot");
    refs.root->set_padding({14.0f, 14.0f, 14.0f, 14.0f})
        .set_spacing(14.0f)
        .set_background({0.045f, 0.052f, 0.070f, 1.0f});

    refs.controls = &ui.make<BoxLayout>(
        Orientation::Vertical, "CompositionControls");
    refs.controls->set_padding({14.0f, 14.0f, 14.0f, 14.0f})
        .set_spacing(10.0f)
        .set_background({0.105f, 0.120f, 0.155f, 1.0f})
        .set_border({0.24f, 0.29f, 0.39f, 1.0f})
        .set_corner_radius(8.0f);
    auto& title = ui.make<Label>("Widgets + TcVisualScene", 18.0f);
    auto& hint = ui.make<Label>(
        "Drag shapes. The green control is a widget portal.", 13.0f);
    refs.status = &ui.make<Label>("Ready", 14.0f);
    refs.reset = &ui.make<Button>("Reset scene");
    refs.controls->add_preferred_child(title);
    refs.controls->add_preferred_child(hint);
    refs.controls->add_preferred_child(*refs.status);
    refs.controls->add_fixed_child(*refs.reset, 34.0f);

    refs.view = &ui.make<SceneView>(refs.scene);
    refs.view->set_min_size({320.0f, 240.0f});
    refs.view->set_scene_colors(
        {0.075f, 0.082f, 0.105f, 1.0f},
        {0.135f, 0.150f, 0.195f, 1.0f},
        {0.28f, 0.34f, 0.45f, 1.0f});
    refs.root->add_fixed_child(*refs.controls, 230.0f);
    refs.root->add_flex_child(*refs.view);

    refs.card = adopt<termin::visual::RoundedRectItem2D>(
        refs.scene,
        nullptr,
        termin::Rect2f{0.0f, 0.0f, 180.0f, 110.0f},
        12.0f,
        fill(0.19f, 0.45f, 0.82f),
        stroke(0.55f, 0.76f, 1.0f, 3.0f));
    set_position(*refs.card, {55.0f, 55.0f});
    refs.card->set_z_order(2);

    adopt<termin::visual::TextItem2D>(
        refs.scene,
        refs.card,
        "shared scene item",
        "ui://default-font",
        termin::Vec2f{16.0f, 34.0f},
        17.0f,
        tgfx::Color4f{0.94f, 0.97f, 1.0f, 1.0f},
        tgfx::TextAnchor2D::Left,
        termin::Bounds2f{0.0f, 0.0f, 160.0f, 48.0f});

    refs.ellipse = adopt<termin::visual::EllipseItem2D>(
        refs.scene,
        nullptr,
        termin::Rect2f{0.0f, 0.0f, 112.0f, 112.0f},
        fill(0.91f, 0.38f, 0.24f),
        stroke(1.0f, 0.70f, 0.48f, 3.0f));
    set_position(*refs.ellipse, {300.0f, 145.0f});
    refs.ellipse->set_z_order(3);

    auto* connector = adopt<termin::visual::PolylineItem2D>(
        refs.scene,
        nullptr,
        std::vector<termin::Vec2f>{
            {145.0f, 110.0f},
            {255.0f, 110.0f},
            {300.0f, 200.0f}},
        stroke(0.52f, 0.61f, 0.78f, 3.0f));
    connector->set_z_order(1);

    auto* clipped = adopt<termin::visual::RoundedRectItem2D>(
        refs.scene,
        nullptr,
        termin::Rect2f{0.0f, 0.0f, 220.0f, 70.0f},
        10.0f,
        fill(0.44f, 0.29f, 0.72f),
        stroke(0.76f, 0.62f, 1.0f, 2.0f));
    set_position(*clipped, {505.0f, 315.0f});
    clipped->set_z_order(0);

    refs.portal = adopt<termin::visual::HitRegionItem2D>(
        refs.scene,
        nullptr,
        rectangle_path(175.0f, 42.0f));
    set_position(*refs.portal, {82.0f, 245.0f});
    refs.portal->set_z_order(10);
    refs.portal_button = &ui.make<Button>("Portal action");
    refs.portal_button->set_accent({0.20f, 0.74f, 0.48f, 1.0f});
    require(
        refs.view->set_widget_portal(
            refs.portal->handle(), refs.portal_button->handle()),
        "failed to attach widget portal");

    const auto state = refs.state;
    Label* const status = refs.status;
    auto* card = refs.card;
    auto* ellipse = refs.ellipse;
    auto* view = refs.view;
    refs.reset->clicked().connect(
        [state, status, card, ellipse, view](Button&) {
            set_position(*card, {55.0f, 55.0f});
            set_position(*ellipse, {300.0f, 145.0f});
            view->invalidate_scene();
            ++state->reset_clicks;
            status->set_text("Scene reset by an ordinary widget");
        });
    refs.portal_button->clicked().connect(
        [state, status](Button& button) {
            ++state->portal_clicks;
            button.set_text(
                "Portal clicked " + std::to_string(state->portal_clicks));
            status->set_text("Portal widget handled the pointer");
        });
    auto scene = refs.scene;
    refs.view->set_pointer_handler(
        [state, status, scene, card, ellipse](
            SceneView& view,
            tc_ui_point world,
            const tc_ui_pointer_event& event) {
            if (event.type == TC_UI_POINTER_DOWN &&
                event.button == 0) {
                const auto hit = termin::visual::hit_test(
                    scene, {world.x, world.y});
                auto* item = hit ? scene.resolve(*hit) : nullptr;
                while (item && item != card->c_item() &&
                       item != ellipse->c_item()) {
                    item = item->parent;
                }
                state->drag_item =
                    item == card->c_item()
                        ? static_cast<termin::visual::GraphicItem2D*>(card)
                        : item == ellipse->c_item()
                            ? static_cast<termin::visual::GraphicItem2D*>(
                                  ellipse)
                            : nullptr;
                if (!state->drag_item) return false;
                state->drag_start_world = world;
                state->drag_start_position =
                    position(*state->drag_item);
                return true;
            }
            if (event.type == TC_UI_POINTER_MOVE &&
                state->drag_item) {
                set_position(
                    *state->drag_item,
                    {
                        state->drag_start_position.x +
                            world.x - state->drag_start_world.x,
                        state->drag_start_position.y +
                            world.y - state->drag_start_world.y,
                    });
                ++state->item_moves;
                status->set_text("Dragged scene item");
                view.invalidate_scene();
                return true;
            }
            if ((event.type == TC_UI_POINTER_UP ||
                 event.type == TC_UI_POINTER_CANCEL) &&
                state->drag_item) {
                state->drag_item = nullptr;
                return true;
            }
            return false;
        });

    return refs;
}

int run_visual_scene_composition_headless_smoke() {
    const tc_ui_document_handle handle = tc_ui_document_create();
    if (!tc_ui_document_is_valid(handle)) {
        std::fprintf(stderr, "visual scene composition: document creation failed\n");
        return 2;
    }
    tc_ui_draw_list* draw_list = nullptr;
    tc_ui_paint_context* context = nullptr;
    try {
        const TcDocument document(handle);
        document.set_text_measurer(&measure_headless_text, nullptr);
        auto refs = build_visual_scene_composition(document);
        document.layout_roots({0.0f, 0.0f, 800.0f, 600.0f});

        require(
            refs.view->bounds().width > 300.0f &&
            refs.controls->bounds().width == 230.0f,
            "mixed layout did not allocate widget panel and scene view");
        require(
            same_widget(
                document.hit_test(
                    refs.reset->bounds().x + 5.0f,
                    refs.reset->bounds().y + 5.0f),
                refs.reset->handle()),
            "ordinary widget routing did not win in the control panel");

        draw_list = tc_ui_draw_list_create();
        context = tc_ui_paint_context_create(draw_list);
        require(draw_list && context, "paint fixture creation failed");
        document.paint_roots(context);

        const std::size_t canvas =
            first_command(draw_list, TC_UI_DRAW_CANVAS2D_LIST);
        const std::size_t count =
            tc_ui_draw_list_command_count(draw_list);
        require(canvas < count, "scene did not emit canonical DrawList2D");
        int clip_depth = 0;
        bool canvas_is_clipped = false;
        bool balanced_clips = true;
        for (std::size_t index = 0; index < count; ++index) {
            const auto* command =
                tc_ui_draw_list_command_at(draw_list, index);
            if (!command) continue;
            if (command->type == TC_UI_DRAW_PUSH_CLIP) {
                ++clip_depth;
            } else if (command->type == TC_UI_DRAW_POP_CLIP) {
                --clip_depth;
                balanced_clips = balanced_clips && clip_depth >= 0;
            } else if (command->type == TC_UI_DRAW_CANVAS2D_LIST) {
                canvas_is_clipped = clip_depth > 0;
            }
        }
        require(
            canvas_is_clipped && balanced_clips && clip_depth == 0,
            "scene and portal painting escaped the SceneView clip");
        require(
            first_command(draw_list, TC_UI_DRAW_TEXT) < canvas,
            "ordinary widget panel was not painted before the scene view");
        std::size_t portal_text = count;
        for (std::size_t index = canvas + 1; index < count; ++index) {
            const auto* command =
                tc_ui_draw_list_command_at(draw_list, index);
            if (command && command->type == TC_UI_DRAW_TEXT) {
                portal_text = index;
                break;
            }
        }
        require(
            portal_text < count,
            "portal widget was not painted after scene graphics");

        const tc_ui_rect portal_bounds = refs.portal_button->bounds();
        const tc_ui_point portal_center{
            portal_bounds.x + portal_bounds.width * 0.5f,
            portal_bounds.y + portal_bounds.height * 0.5f};
        require(
            same_widget(
                document.hit_test(portal_center.x, portal_center.y),
                refs.portal_button->handle()),
            "portal widget did not win over its graphic hit region");
        const auto portal_position = position(*refs.portal);
        click(document, portal_center);
        require(
            refs.state->portal_clicks == 1 &&
            near(position(*refs.portal).x, portal_position.x) &&
            near(position(*refs.portal).y, portal_position.y),
            "portal click leaked into scene dragging");

        const tc_ui_point card_start =
            refs.view->world_to_screen({80.0f, 80.0f});
        const tc_ui_point card_end{
            card_start.x + 37.0f, card_start.y + 23.0f};
        require(
            document.dispatch_pointer_event(
                {TC_UI_POINTER_DOWN, card_start.x, card_start.y,
                 0, 1, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED,
            "scene drag down was not handled");
        require(
            document.dispatch_pointer_event(
                {TC_UI_POINTER_MOVE, card_end.x, card_end.y,
                 0, 0, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED,
            "scene drag move was not handled");
        require(
            document.dispatch_pointer_event(
                {TC_UI_POINTER_UP, card_end.x, card_end.y,
                 0, 1, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED,
            "scene drag up was not handled");
        require(
            refs.state->item_moves == 1 &&
            near(position(*refs.card).x, 92.0f) &&
            near(position(*refs.card).y, 78.0f),
            "scene item did not follow captured drag");

        refs.view->clear_dirty(TC_WIDGET_DIRTY_MASK);
        set_position(*refs.ellipse, {325.0f, 165.0f});
        refs.view->invalidate_scene();
        require(
            refs.view->has_dirty_flags(
                TC_WIDGET_DIRTY_LAYOUT | TC_WIDGET_DIRTY_PAINT),
            "scene invalidation did not reach SceneView");

        const tc_ui_rect old_view = refs.view->bounds();
        document.layout_roots({0.0f, 0.0f, 1024.0f, 640.0f});
        require(
            refs.view->bounds().width > old_view.width,
            "SceneView did not resize with the document");
        const tc_ui_rect resized_portal = refs.portal_button->bounds();
        const tc_ui_point expected_portal =
            refs.view->world_to_screen(position(*refs.portal));
        require(
            near(resized_portal.x, expected_portal.x) &&
            near(resized_portal.y, expected_portal.y),
            "portal layout diverged from the scene transform after resize");

        click(
            document,
            {
                refs.reset->bounds().x + refs.reset->bounds().width * 0.5f,
                refs.reset->bounds().y + refs.reset->bounds().height * 0.5f,
            });
        require(
            refs.state->reset_clicks == 1 &&
            near(position(*refs.card).x, 55.0f) &&
            near(position(*refs.ellipse).x, 300.0f),
            "ordinary widget did not update scene state");

        tc_ui_paint_context_destroy(context);
        context = nullptr;
        tc_ui_draw_list_destroy(draw_list);
        draw_list = nullptr;
        tc_ui_document_destroy(handle);
        std::puts(
            "visual scene composition smoke: widgets, scene and portal passed");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(
            stderr,
            "visual scene composition smoke failed: %s\n",
            error.what());
        if (context) tc_ui_paint_context_destroy(context);
        if (draw_list) tc_ui_draw_list_destroy(draw_list);
        tc_ui_document_destroy(handle);
        return 1;
    }
}

} // namespace termin::gui_native::examples
