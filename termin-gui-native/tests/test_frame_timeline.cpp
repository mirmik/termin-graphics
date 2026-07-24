#include <termin/gui_native/tc_document.hpp>
#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cstdlib>
#include <stdexcept>

using namespace termin::gui_native;

namespace {

std::vector<FrameTimelineSample> samples(int first, int count) {
    std::vector<FrameTimelineSample> result;
    for (int index = 0; index < count; ++index) {
        result.push_back(FrameTimelineSample{
            first + index,
            16.0f + static_cast<float>(index),
            5.0f,
            static_cast<float>(index),
            16.0f,
            index == count - 2,
            false,
        });
    }
    return result;
}

void test_model_validation_and_selection_reconciliation() {
    auto model = std::make_shared<FrameTimelineModel>();
    FrameTimelineWidget timeline(model);
    std::vector<int64_t> selections;
    timeline.selection_changed().connect(
        [&selections](FrameTimelineWidget&, int64_t stable_id) {
            selections.push_back(stable_id);
        }
    );
    model->set_samples(samples(100, 20));
    assert(timeline.selected_id() == 119);
    timeline.set_window_size(8);
    assert((timeline.visible_range() == std::pair<size_t, size_t>{12, 20}));
    timeline.set_scroll_offset(5);
    assert(!timeline.follow_latest());
    assert((timeline.visible_range() == std::pair<size_t, size_t>{7, 15}));
    assert(timeline.select(108));
    model->set_samples(samples(200, 4));
    assert(!timeline.selected_id());
    assert(selections.back() == -1);

    bool duplicate_rejected = false;
    try {
        model->set_samples({FrameTimelineSample{1}, FrameTimelineSample{1}});
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    assert(duplicate_rejected);
}

void test_pointer_keyboard_zoom_and_visible_projection() {
    tc_ui_document_handle document_handle = tc_ui_document_create();
    TcDocument document(document_handle);
    auto model = std::make_shared<FrameTimelineModel>();
    auto* timeline = new FrameTimelineWidget(model);
    assert(!tc_widget_handle_is_invalid(document.adopt(timeline)));
    assert(document.add_root(*timeline));
    model->set_samples(samples(1, 30));
    timeline->set_window_size(10);
    timeline->set_scroll_offset(5);
    document.layout_roots({0.0f, 0.0f, 300.0f, 180.0f});

    tc_ui_pointer_event click{};
    click.type = TC_UI_POINTER_DOWN;
    click.x = 15.0f;
    click.y = 60.0f;
    assert(document.dispatch_pointer_event(click) == TC_UI_EVENT_HANDLED);
    assert(timeline->selected_id() == 16);

    tc_ui_key_event key{};
    key.type = TC_UI_KEY_DOWN;
    key.key = TC_UI_KEY_RIGHT;
    assert(document.dispatch_key_event(key) == TC_UI_EVENT_HANDLED);
    assert(timeline->selected_id() == 17);

    tc_ui_pointer_event wheel{};
    wheel.type = TC_UI_POINTER_WHEEL;
    wheel.x = 100.0f;
    wheel.y = 80.0f;
    wheel.wheel_y = -1.0f;
    wheel.modifiers = TC_UI_MOD_CTRL;
    assert(document.dispatch_pointer_event(wheel) == TC_UI_EVENT_HANDLED);
    assert(timeline->window_size() == 22);
    tc_ui_document_destroy(document_handle);
}

void test_model_incremental_append_and_capacity() {
    auto model = std::make_shared<FrameTimelineModel>();
    model->set_samples(samples(10, 3));
    const uint64_t revision = model->revision();

    model->append_samples(samples(13, 3), 4);
    assert(model->revision() == revision + 1);
    assert(model->samples().size() == 4);
    assert(model->samples().front().stable_id == 12);
    assert(model->samples().back().stable_id == 15);

    model->append_samples({}, 4);
    assert(model->revision() == revision + 1);

    bool overlapping_rejected = false;
    try {
        model->append_samples(samples(15, 1), 4);
    } catch (const std::invalid_argument&) {
        overlapping_rejected = true;
    }
    assert(overlapping_rejected);
}

} // namespace

int main() {
    test_model_validation_and_selection_reconciliation();
    test_pointer_keyboard_zoom_and_visible_projection();
    test_model_incremental_append_and_capacity();
    return EXIT_SUCCESS;
}
