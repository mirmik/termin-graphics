#include <termin/gui_native/box_layout.hpp>
#include <termin/gui_native/icon_button.hpp>
#include <termin/gui_native/uiscript.hpp>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "widgets_test_support.hpp"

using namespace termin::gui_native;

namespace {

    struct MatrixCase {
        const char* name;
        float density;
        float font_scale;
        tc_ui_size logical_extent;
        tc_ui_insets logical_safe_insets;
        Orientation expected_orientation;
        float expected_hud_width;
    };

    [[noreturn]] void fail(const MatrixCase& test_case, const char* check) {
        std::fprintf(stderr,
                     "adaptive layout matrix failed: case=%s density=%.2f font=%.2f "
                     "viewport=%.1fx%.1f check=%s\n",
                     test_case.name,
                     test_case.density,
                     test_case.font_scale,
                     test_case.logical_extent.width,
                     test_case.logical_extent.height,
                     check);
        std::abort();
    }

    void require(bool condition, const MatrixCase& test_case, const char* check) {
        if (!condition) {
            fail(test_case, check);
        }
    }

    bool near(float lhs, float rhs) {
        return std::fabs(lhs - rhs) < 0.001f;
    }

    tc_ui_presentation_metrics metrics_for(const MatrixCase& test_case) {
        return tc_ui_presentation_metrics{
            test_case.density,
            test_case.font_scale,
            tc_ui_size{
                test_case.logical_extent.width * test_case.density,
                test_case.logical_extent.height * test_case.density,
            },
            tc_ui_insets{
                test_case.logical_safe_insets.left * test_case.density,
                test_case.logical_safe_insets.top * test_case.density,
                test_case.logical_safe_insets.right * test_case.density,
                test_case.logical_safe_insets.bottom * test_case.density,
            },
        };
    }

    tc_widget* widget(LoadedUiScript& loaded, const char* name) {
        return tc_ui_document_resolve_widget(loaded.document().handle(), loaded.named(name).handle);
    }

    float run_case(LoadedUiScript& loaded, BoxLayout& hud, IconButton& action, const MatrixCase& test_case) {
        TcDocument document = loaded.document();
        const tc_ui_presentation_metrics metrics = metrics_for(test_case);
        require(document.set_presentation_metrics(metrics), test_case, "presentation metrics rejected");

        tc_ui_rect layout_rect{};
        require(document.presentation_layout_rect(layout_rect), test_case, "safe layout rect unavailable");
        document.layout_roots(layout_rect);

        tc_widget* root = widget(loaded, "native_runtime_overlay");
        tc_widget* hint = widget(loaded, "hint");
        require(root && hint, test_case, "named showcase widgets missing");
        const tc_ui_rect root_bounds = tc_widget_bounds(root);
        const tc_ui_rect hud_bounds = hud.bounds();
        const tc_ui_rect hint_bounds = tc_widget_bounds(hint);
        const tc_ui_rect button_bounds = action.bounds();

        require(near(root_bounds.x, test_case.logical_safe_insets.left) &&
                    near(root_bounds.y, test_case.logical_safe_insets.top) &&
                    near(root_bounds.width,
                         test_case.logical_extent.width - test_case.logical_safe_insets.left -
                             test_case.logical_safe_insets.right) &&
                    near(root_bounds.height,
                         test_case.logical_extent.height - test_case.logical_safe_insets.top -
                             test_case.logical_safe_insets.bottom),
                test_case,
                "root does not match logical safe rect");
        require(hud.orientation() == test_case.expected_orientation, test_case, "responsive orientation mismatch");
        require(near(hud_bounds.width, test_case.expected_hud_width), test_case, "responsive HUD width mismatch");
        require(hud_bounds.x >= root_bounds.x && hud_bounds.y >= root_bounds.y &&
                    hud_bounds.x + hud_bounds.width <= root_bounds.x + root_bounds.width &&
                    hud_bounds.y + hud_bounds.height <= root_bounds.y + root_bounds.height,
                test_case,
                "HUD escaped safe bounds");
        require(
            button_bounds.width >= 48.0f && button_bounds.height >= 48.0f, test_case, "minimum touch target was lost");
        require(hint_bounds.x >= hud_bounds.x && hint_bounds.y >= hud_bounds.y &&
                    hint_bounds.x + hint_bounds.width <= hud_bounds.x + hud_bounds.width &&
                    hint_bounds.y + hint_bounds.height <= hud_bounds.y + hud_bounds.height,
                test_case,
                "wrapped hint escaped HUD bounds");
        require(action.active(), test_case, "button state was lost during reflow");

        const tc_ui_point logical_button_center{
            button_bounds.x + button_bounds.width * 0.5f,
            button_bounds.y + button_bounds.height * 0.5f,
        };
        tc_ui_point converted{};
        require(tc_ui_presentation_metrics_physical_to_logical_point(&metrics,
                                                                     tc_ui_point{
                                                                         logical_button_center.x * test_case.density,
                                                                         logical_button_center.y * test_case.density,
                                                                     },
                                                                     &converted) &&
                    near(converted.x, logical_button_center.x) && near(converted.y, logical_button_center.y),
                test_case,
                "fractional physical/logical pointer conversion diverged");

        tc_ui_pointer_event pointer{};
        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = converted.x;
        pointer.y = converted.y;
        require(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED,
                test_case,
                "button did not consume converted pointer");
        pointer.type = TC_UI_POINTER_UP;
        require(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_HANDLED,
                test_case,
                "button did not release converted pointer");

        pointer.type = TC_UI_POINTER_DOWN;
        pointer.x = root_bounds.x + root_bounds.width - 2.0f;
        pointer.y = root_bounds.y + root_bounds.height - 2.0f;
        require(document.dispatch_pointer_event(pointer) == TC_UI_EVENT_IGNORED,
                test_case,
                "uncovered scene pointer was consumed");

        return hud_bounds.height;
    }

} // namespace

int main() {
    const std::string showcase_path = std::string(TERMIN_GUI_NATIVE_SOURCE_DIR) +
                                      "/../test-projects/android-render-showcase/"
                                      "UI/native_runtime_hud.uiscript";
    UiScriptLoader loader;
    LoadedUiScript loaded = loader.load(showcase_path);
    termin_gui_native_test::install_test_text_measurer(loaded.document());

    tc_widget* hud_widget = widget(loaded, "hud");
    tc_widget* action_widget = widget(loaded, "action");
    if (!hud_widget || !action_widget) {
        std::fprintf(stderr, "adaptive layout matrix: showcase HUD is incomplete\n");
        return 1;
    }
    auto& hud = *static_cast<BoxLayout*>(hud_widget->body);
    auto& action = *static_cast<IconButton*>(action_widget->body);
    action.set_active(true);

    const std::array<MatrixCase, 8> cases{{
        {"compact-1x", 1.0f, 1.0f, {360.0f, 640.0f}, {4.0f, 12.0f, 6.0f, 20.0f}, Orientation::Vertical, 300.0f},
        {"compact-1.5x", 1.5f, 1.0f, {360.0f, 640.0f}, {4.0f, 12.0f, 6.0f, 20.0f}, Orientation::Vertical, 300.0f},
        {"compact-2x", 2.0f, 1.0f, {360.0f, 640.0f}, {4.0f, 12.0f, 6.0f, 20.0f}, Orientation::Vertical, 300.0f},
        {"compact-3x", 3.0f, 1.0f, {360.0f, 640.0f}, {4.0f, 12.0f, 6.0f, 20.0f}, Orientation::Vertical, 300.0f},
        {"medium-portrait", 2.0f, 1.0f, {700.0f, 1000.0f}, {0.0f, 24.0f, 0.0f, 24.0f}, Orientation::Vertical, 420.0f},
        {"expanded-tablet", 2.0f, 1.0f, {900.0f, 1200.0f}, {0.0f, 24.0f, 0.0f, 24.0f}, Orientation::Vertical, 520.0f},
        {"phone-landscape", 3.0f, 1.0f, {800.0f, 360.0f}, {20.0f, 0.0f, 24.0f, 10.0f}, Orientation::Horizontal, 560.0f},
        {"desktop-identity", 1.0f, 1.0f, {1024.0f, 768.0f}, {}, Orientation::Horizontal, 560.0f},
    }};
    for (const MatrixCase& test_case : cases) {
        run_case(loaded, hud, action, test_case);
    }

    float previous_height = 0.0f;
    for (const float font_scale : {1.0f, 1.3f, 1.5f}) {
        const MatrixCase test_case{
            "accessibility-font",
            2.0f,
            font_scale,
            {360.0f, 640.0f},
            {4.0f, 12.0f, 6.0f, 20.0f},
            Orientation::Vertical,
            300.0f,
        };
        const float height = run_case(loaded, hud, action, test_case);
        require(height >= previous_height, test_case, "font scale reduced laid-out HUD height");
        previous_height = height;
    }
    return 0;
}
