#include <termin/gui_native/widgets.hpp>

#include <cassert>
#include <cstdlib>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

using namespace termin::gui_native;

namespace {

struct HostTrace {
    bool valid = true;
    uint32_t texture = 73;
    ViewportSurfaceSize size{64, 64};
    std::vector<std::string> ordering;
    std::vector<std::tuple<double, double>> moves;
    std::vector<std::tuple<int, int, int, uint32_t>> buttons;
    std::vector<std::tuple<double, double, int>> scrolls;
    std::vector<std::tuple<int, int, int, int>> keys;
    std::vector<uint32_t> text;
};

class TestSurfaceHost final : public ViewportSurfaceHost {
public:
    explicit TestSurfaceHost(std::shared_ptr<HostTrace> trace) : trace_(std::move(trace)) {}

    bool is_valid() const override { return trace_->valid; }
    uint32_t texture_id() const override { return trace_->texture; }
    ViewportSurfaceSize framebuffer_size() const override { return trace_->size; }
    bool resize(int width, int height) override {
        trace_->ordering.emplace_back("resize");
        trace_->size = {width, height};
        return true;
    }
    bool pointer_move(double x, double y) override {
        trace_->moves.emplace_back(x, y);
        return true;
    }
    bool pointer_button(int button, int action, int modifiers, uint32_t click_count) override {
        trace_->buttons.emplace_back(button, action, modifiers, click_count);
        return true;
    }
    bool scroll(double x, double y, int modifiers) override {
        trace_->scrolls.emplace_back(x, y, modifiers);
        return true;
    }
    bool key(int key, int scancode, int action, int modifiers) override {
        trace_->keys.emplace_back(key, scancode, action, modifiers);
        return true;
    }
    bool text(uint32_t codepoint) override {
        trace_->text.push_back(codepoint);
        return true;
    }

private:
    std::shared_ptr<HostTrace> trace_;
};

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

void test_surface_resize_paint_input_and_drag_contract() {
    Document document;
    auto trace = std::make_shared<HostTrace>();
    auto host = std::make_shared<TestSurfaceHost>(trace);
    auto* viewport = new Viewport3D();
    const tc_widget_handle handle = document.adopt(viewport);
    assert(document.add_root(*viewport));
    viewport->before_resize().connect(
        [trace](Viewport3D&, ViewportSurfaceSize previous, ViewportSurfaceSize next) {
            assert((previous == ViewportSurfaceSize{64, 64}));
            assert((next == ViewportSurfaceSize{300, 180}));
            trace->ordering.emplace_back("before");
        });
    document.layout_roots(tc_ui_rect{10.0f, 20.0f, 300.8f, 180.9f});
    viewport->set_surface_host(host);
    assert((trace->ordering == std::vector<std::string>{"before", "resize"}));
    assert((trace->size == ViewportSurfaceSize{300, 180}));

    tc_ui_draw_list* draw_list = tc_ui_draw_list_create();
    tc_ui_paint_context* context = tc_ui_paint_context_create(draw_list);
    document.paint_roots(context);
    const auto textures = commands_of_type(draw_list, TC_UI_DRAW_TEXTURE);
    assert(textures.size() == 1);
    assert(textures[0]->texture_id == 73);
    assert(!textures[0]->flip_v);

    assert(document.dispatch_pointer_event(tc_ui_pointer_event{
               TC_UI_POINTER_DOWN, 42.0f, 65.0f, 1, 2, 7, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert((trace->moves.back() == std::tuple<double, double>{32.0, 45.0}));
    assert((trace->buttons.back() == std::tuple<int, int, int, uint32_t>{1, 1, 7, 2}));
    assert(document.dispatch_pointer_event(tc_ui_pointer_event{
               TC_UI_POINTER_WHEEL, 14.0f, 26.0f, 0, 0, 5, 6.0f, -2.0f}) == TC_UI_EVENT_HANDLED);
    assert((trace->moves.back() == std::tuple<double, double>{4.0, 6.0}));
    assert((trace->scrolls.back() == std::tuple<double, double, int>{6.0, -2.0, 5}));

    assert(document.dispatch_key_event(tc_ui_key_event{TC_UI_KEY_DOWN, 65, 9, 3, true}) ==
           TC_UI_EVENT_HANDLED);
    assert((trace->keys.back() == std::tuple<int, int, int, int>{65, 9, 2, 3}));
    assert(document.dispatch_text_event(tc_ui_text_event{"A\xD0\x96"}) == TC_UI_EVENT_HANDLED);
    assert((trace->text == std::vector<uint32_t>{65, 0x416}));

    ViewportExternalDragEvent drag{ViewportExternalDragPhase::Drop, "text/uri-list",
                                   "file:///tmp/scene.tscene", 12.0f, 14.0f};
    bool drag_called = false;
    viewport->set_external_drag_handler([&drag_called](const ViewportExternalDragEvent& event) {
        drag_called = true;
        return event.phase == ViewportExternalDragPhase::Drop && event.x == 12.0f;
    });
    assert(viewport->dispatch_external_drag(drag));
    assert(drag_called);

    tc_ui_paint_context_destroy(context);
    tc_ui_draw_list_destroy(draw_list);
    assert(tc_ui_document_destroy_widget(document.get(), handle));
}

void test_detach_destroy_and_stale_surface_are_safe() {
    Document document;
    auto trace = std::make_shared<HostTrace>();
    std::weak_ptr<TestSurfaceHost> weak_host;
    auto* viewport = new Viewport3D();
    const tc_widget_handle handle = document.adopt(viewport);
    assert(document.add_root(*viewport));
    {
        auto host = std::make_shared<TestSurfaceHost>(trace);
        weak_host = host;
        viewport->set_surface_host(host);
    }
    assert(!weak_host.expired());
    trace->valid = false;
    assert(!viewport->surface_valid());
    assert(viewport->texture_id() == 0);
    document.layout_roots(tc_ui_rect{0.0f, 0.0f, 200.0f, 100.0f});
    assert(trace->ordering.empty());
    assert(document.dispatch_pointer_event(tc_ui_pointer_event{
               TC_UI_POINTER_MOVE, 5.0f, 6.0f, 0, 0, 0, 0.0f, 0.0f}) == TC_UI_EVENT_HANDLED);
    assert(trace->moves.empty());

    viewport->detach_surface();
    assert(!viewport->has_surface());
    assert(weak_host.expired());
    assert(tc_ui_document_destroy_widget(document.get(), handle));

    auto trace2 = std::make_shared<HostTrace>();
    auto host2 = std::make_shared<TestSurfaceHost>(trace2);
    std::weak_ptr<TestSurfaceHost> weak_host2 = host2;
    auto* viewport2 = new Viewport3D();
    const tc_widget_handle handle2 = document.adopt(viewport2);
    viewport2->set_surface_host(host2);
    host2.reset();
    assert(!weak_host2.expired());
    assert(tc_ui_document_destroy_widget(document.get(), handle2));
    assert(weak_host2.expired());
}

} // namespace

int main() {
    test_surface_resize_paint_input_and_drag_contract();
    test_detach_destroy_and_stale_surface_are_safe();
    return EXIT_SUCCESS;
}
