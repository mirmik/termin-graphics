#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

using namespace termin::gui_native;

namespace {

bool measure_text(void*, const char*, size_t byte_length, float font_size,
                  tc_ui_text_metrics* metrics) {
    if (!metrics || font_size <= 0.0f)
        return false;
    metrics->width = static_cast<float>(byte_length) * font_size * 0.5f;
    metrics->height = font_size;
    metrics->ascent = font_size * 0.8f;
    metrics->descent = font_size * 0.2f;
    metrics->line_height = font_size * 1.2f;
    return true;
}

std::vector<const tc_ui_draw_command*> commands_of_type(const tc_ui_draw_list* draw_list,
                                                        tc_ui_draw_command_type type) {
    std::vector<const tc_ui_draw_command*> result;
    for (size_t index = 0; index < tc_ui_draw_list_command_count(draw_list); ++index) {
        const tc_ui_draw_command* command = tc_ui_draw_list_command_at(draw_list, index);
        if (command && command->type == type)
            result.push_back(command);
    }
    return result;
}

void test_model_bounds_history_and_validates_samples() {
    FrameTimeModel model;
    size_t notifications = 0;
    model.changed().connect([&notifications](FrameTimeModel&) { ++notifications; });
    model.set_max_samples(3);
    model.add_sample(10.0f);
    model.add_sample(20.0f);
    model.add_sample(30.0f);
    model.add_sample(40.0f);
    assert((model.samples() == std::vector<float>{20.0f, 30.0f, 40.0f}));
    assert(notifications == 5);
    model.set_max_samples(2);
    assert((model.samples() == std::vector<float>{30.0f, 40.0f}));

    bool rejected = false;
    try {
        model.add_sample(-1.0f);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
    assert(model.samples().size() == 2);
    model.clear();
    assert(model.samples().empty());
}

void test_graph_empty_and_threshold_bars_are_deterministic() {
    Document document;
    document.set_text_measurer(&measure_text, nullptr);
    auto model = std::make_shared<FrameTimeModel>();
    model->set_max_samples(3);
    auto* graph = new FrameTimeGraph(model);
    const tc_widget_handle handle = document.adopt(graph);
    assert(!tc_widget_handle_is_invalid(handle));
    assert(document.add_root(*graph));
    document.layout_roots(tc_ui_rect{5.0f, 7.0f, 300.0f, 80.0f});

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(context);
    assert(commands_of_type(draw_list, TC_UI_DRAW_FILL_RECT).size() == 1);
    const auto empty_text = commands_of_type(draw_list, TC_UI_DRAW_TEXT);
    assert(empty_text.size() == 1);
    assert(std::string(empty_text[0]->text) == "No profiler data");

    tc_ui_draw_list_clear(draw_list);
    model->set_samples({10.0f, 20.0f, 40.0f});
    assert((tc_widget_dirty_flags(graph->c_widget()) & TC_WIDGET_DIRTY_PAINT) != 0);
    document.paint_roots(context);
    const auto fills = commands_of_type(draw_list, TC_UI_DRAW_FILL_RECT);
    assert(fills.size() == 4);
    assert(fills[1]->color.g > fills[1]->color.r);
    assert(fills[2]->color.r > 0.7f && fills[2]->color.g > 0.6f);
    assert(fills[3]->color.r > fills[3]->color.g);
    assert(std::fabs(fills[1]->rect.x - 5.0f) < 0.001f);
    assert(std::fabs(fills[3]->rect.x - 205.0f) < 0.001f);
    assert(commands_of_type(draw_list, TC_UI_DRAW_LINE).size() == 3);
    const auto labels = commands_of_type(draw_list, TC_UI_DRAW_TEXT);
    assert(labels.size() == 2);
    assert(std::string(labels[0]->text) == "60");
    assert(std::string(labels[1]->text) == "30");

    model.reset();
    assert(graph->model()->samples().size() == 3);
    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
}

} // namespace

int main() {
    test_model_bounds_history_and_validates_samples();
    test_graph_empty_and_threshold_bars_are_deterministic();
    return EXIT_SUCCESS;
}
