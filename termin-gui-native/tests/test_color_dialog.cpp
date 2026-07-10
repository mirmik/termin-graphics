#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>

using namespace termin::gui_native;

namespace {

bool near(float left, float right, float epsilon = 0.01f) {
    return std::fabs(left - right) <= epsilon;
}

bool measure_text(void*, const char*, size_t byte_length, float font_size,
                  tc_ui_text_metrics* metrics) {
    if (!metrics)
        return false;
    metrics->width = static_cast<float>(byte_length) * font_size * 0.5f;
    metrics->height = font_size;
    metrics->ascent = font_size * 0.8f;
    metrics->descent = font_size * 0.2f;
    metrics->line_height = font_size * 1.2f;
    return true;
}

size_t count_commands(const tc_ui_draw_list* list, tc_ui_draw_command_type type) {
    size_t count = 0;
    for (size_t index = 0; index < tc_ui_draw_list_command_count(list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(list, index);
        if (command && command->type == type)
            ++count;
    }
    return count;
}

void test_color_model_round_trip_and_revisions() {
    ColorPickerModel model(Color{1.0f, 0.0f, 0.0f, 0.5f}, true);
    assert(near(model.hue(), 0.0f));
    assert(near(model.saturation(), 1.0f));
    assert(near(model.value(), 1.0f));
    assert(near(model.alpha(), 0.5f));

    std::vector<uint32_t> changes;
    model.changed().connect(
        [&changes](ColorPickerModel&, uint32_t flags) { changes.push_back(flags); });
    model.set_hue(0.5f);
    const Color cyan = model.color();
    assert(near(cyan.r, 0.0f));
    assert(near(cyan.g, 1.0f));
    assert(near(cyan.b, 1.0f));
    assert((changes.back() & color_picker_change_mask(ColorPickerChange::SvSurface)) != 0);
    assert((changes.back() & color_picker_change_mask(ColorPickerChange::AlphaSurface)) != 0);

    model.set_show_alpha(false);
    assert(near(model.color().a, 1.0f));
    model.set_alpha(0.25f);
    assert(near(model.alpha(), 0.25f));
    assert(near(model.color().a, 1.0f));

    bool rejected = false;
    try {
        model.set_value(1.1f);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void test_picker_surfaces_paint_and_pointer_capture() {
    Document document;
    document.set_text_measurer(&measure_text, nullptr);
    DocumentBuilder ui(document);
    auto model = std::make_shared<ColorPickerModel>(Color{1.0f, 0.0f, 0.0f, 1.0f}, true);
    auto& picker = ui.make_root<ColorPicker>(model);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 250.0f, 244.0f});

    const ColorPickerSurface& hue = picker.surface(ColorPickerSurfaceKind::Hue);
    const ColorPickerSurface& sv = picker.surface(ColorPickerSurfaceKind::SaturationValue);
    const ColorPickerSurface& alpha = picker.surface(ColorPickerSurfaceKind::Alpha);
    assert(hue.width == 1 && hue.height == 64 && hue.rgba.size() == 256);
    assert(sv.width == 64 && sv.height == 64 && sv.rgba.size() == 64 * 64 * 4);
    assert(alpha.width == 1 && alpha.height == 64 && alpha.rgba.size() == 256);
    const uint64_t old_sv_revision = sv.revision;
    uint32_t invalidated = 0;
    picker.surfaces_invalidated().connect(
        [&invalidated](ColorPicker&, uint32_t flags) { invalidated |= flags; });
    model->set_hue(0.25f);
    assert(invalidated != 0);
    assert(picker.surface(ColorPickerSurfaceKind::SaturationValue).revision > old_sv_revision);

    tc_ui_draw_list* list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(list);
    picker.paint(document.get(), context);
    assert(count_commands(list, TC_UI_DRAW_FILL_RECT) > 600);
    assert(tc_ui_draw_list_command_count(list) < 1000);
    tc_ui_draw_list_clear(list);
    picker.set_texture_ids(ColorPickerTextureIds{11, 12, 13});
    picker.paint(document.get(), context);
    assert(count_commands(list, TC_UI_DRAW_TEXTURE) == 3);
    assert(tc_ui_draw_list_command_count(list) < 200);
    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(list);

    tc_ui_pointer_event pointer{};
    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = 90.0f;
    pointer.y = 90.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_eq(document.pointer_capture(), picker.handle()));
    assert(near(model->saturation(), 0.5f));
    assert(near(model->value(), 0.5f));
    pointer.type = TC_UI_POINTER_MOVE;
    pointer.x = 300.0f;
    pointer.y = -20.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(near(model->saturation(), 1.0f));
    assert(near(model->value(), 1.0f));
    pointer.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(tc_widget_handle_is_invalid(document.pointer_capture()));

    pointer.type = TC_UI_POINTER_DOWN;
    pointer.x = 225.0f;
    pointer.y = 179.0f;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
    assert(near(model->alpha(), 0.0f));
    pointer.type = TC_UI_POINTER_UP;
    assert(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED);
}

void test_color_dialog_typed_result_and_cancel() {
    Document document;
    document.set_text_measurer(&measure_text, nullptr);
    DocumentBuilder ui(document);
    auto& dialog = ui.make<ColorDialog>(Color{1.0f, 0.0f, 0.0f, 0.5f}, true);
    std::vector<std::optional<Color>> results;
    dialog.color_finished().connect(
        [&results](ColorDialog&, const std::optional<Color>& color) { results.push_back(color); });

    assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
    dialog.set_color(Color{0.0f, 0.5f, 1.0f, 0.25f});
    assert(dialog.activate("ok", document.get()));
    assert(results.size() == 1 && results[0]);
    assert(near(results[0]->g, 0.5f));
    assert(near(results[0]->a, 0.25f));

    assert(dialog.show(document.get(), tc_ui_rect{0.0f, 0.0f, 640.0f, 480.0f}));
    assert(dialog.activate("cancel", document.get()));
    assert(results.size() == 2 && !results[1]);
}

} // namespace

int main() {
    test_color_model_round_trip_and_revisions();
    test_picker_surfaces_paint_and_pointer_capture();
    test_color_dialog_typed_result_and_cancel();
    return EXIT_SUCCESS;
}
