#include <termin/gui_native/document.hpp>
#include <termin/gui_native/h_stack.hpp>
#include <termin/gui_native/icon_button.hpp>
#include <termin/gui_native/overlay_layout.hpp>
#include <termin/gui_native/viewport3d.hpp>

#include <cassert>

using namespace termin::gui_native;

int main() {
    Document document;
    auto* composition = new OverlayLayout("viewport-composition");
    auto* viewport = new Viewport3D();
    auto* overlay = new OverlayLayout("controls-overlay");
    auto* top_right = new HStack("top-right");
    auto* button = new IconButton("T");
    button->set_preferred_size({26.0f, 26.0f});

    assert(!tc_widget_handle_is_invalid(document.adopt(composition)));
    assert(!tc_widget_handle_is_invalid(document.adopt(viewport)));
    assert(!tc_widget_handle_is_invalid(document.adopt(overlay)));
    assert(!tc_widget_handle_is_invalid(document.adopt(top_right)));
    assert(!tc_widget_handle_is_invalid(document.adopt(button)));
    assert(composition->add_child(viewport->handle(), OverlayAnchor::Fill));
    assert(composition->add_child(overlay->handle(), OverlayAnchor::Fill));
    assert(overlay->add_child(
        top_right->handle(), OverlayAnchor::TopRight, {-10.0f, 10.0f}));
    top_right->add_preferred_child(button->handle());
    assert(document.add_root(*composition));

    document.layout_roots({0.0f, 0.0f, 800.0f, 600.0f});
    assert(viewport->bounds().width == 800.0f);
    assert(button->bounds().x == 764.0f);
    assert(button->bounds().y == 10.0f);

    document.layout_roots({5.0f, 7.0f, 1000.0f, 700.0f});
    assert(viewport->bounds().x == 5.0f);
    assert(viewport->bounds().width == 1000.0f);
    assert(button->bounds().x == 969.0f);
    assert(button->bounds().y == 17.0f);

    const tc_widget_handle overlay_hit = document.hit_test(975.0f, 20.0f);
    assert(tc_widget_handle_eq(overlay_hit, button->handle()));
    const tc_widget_handle viewport_hit = document.hit_test(200.0f, 200.0f);
    assert(tc_widget_handle_eq(viewport_hit, viewport->handle()));

    assert(document.remove_root(*composition));
    assert(tc_ui_document_destroy_widget_recursive(document.get(), composition->handle()));
    assert(tc_ui_document_live_widget_count(document.get()) == 0);
    return 0;
}
