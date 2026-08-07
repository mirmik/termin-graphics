#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <limits>
#include <variant>
#include <vector>

#include <termin_visual_scene/scene_render2d.hpp>

#include "tcplot/plot_grid_item2d.hpp"

namespace {

    class Resolver final : public termin::visual::SceneRenderResourceResolver2D {
    public:
        std::optional<tgfx::FontHandle> resolve_font(std::string_view) override {
            return std::nullopt;
        }
        std::optional<tgfx::TextureHandle> resolve_image(std::string_view) override {
            return std::nullopt;
        }
        std::optional<termin::visual::ResolvedCustomBatch2D> resolve_custom_batch(std::string_view,
                                                                                  termin::Bounds2f) override {
            return std::nullopt;
        }
    };

    tcplot::PlotFrame2D
    frame(float x, float y, float width, float height, double x_min, double x_max, double y_min, double y_max) {
        return {
            {x, y, width, height},
            {x + 20.0f, y + 10.0f, width - 30.0f, height - 30.0f},
            {x_min, x_max, y_min, y_max},
            {x + 20.0f, y + 10.0f, width - 30.0f, height - 30.0f},
            1.0f,
        };
    }

    const tgfx::DrawPath2D& only_path(const tgfx::DrawList2D& list) {
        const tgfx::DrawPath2D* result = nullptr;
        for (const auto& command : list.commands()) {
            if (const auto* path = std::get_if<tgfx::DrawPath2D>(&command)) {
                assert(result == nullptr);
                result = path;
            }
        }
        assert(result != nullptr);
        return *result;
    }

    const tgfx::PushClip2D& only_clip(const tgfx::DrawList2D& list) {
        const tgfx::PushClip2D* result = nullptr;
        for (const auto& command : list.commands()) {
            if (const auto* clip = std::get_if<tgfx::PushClip2D>(&command)) {
                assert(result == nullptr);
                result = clip;
            }
        }
        assert(result != nullptr);
        return *result;
    }

} // namespace

int main() {
    using namespace tcplot;
    using termin::visual::TcVisualScene;

    const auto scene_handle = tc_visual_scene_create();
    const auto other_scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};
    TcVisualScene other_scene{other_scene_handle};

    auto projection = create_plot_projection2d(scene, frame(5.0f, 7.0f, 400.0f, 300.0f, 0.0, 10.0, -5.0, 5.0));
    auto foreign_projection =
        create_plot_projection2d(other_scene, frame(0.0f, 0.0f, 100.0f, 100.0f, 0.0, 1.0, 0.0, 1.0));
    assert(projection && foreign_projection);

    const std::vector<double> x_ticks{-100.0, 0.0, 5.0, 10.0, 100.0};
    const std::vector<double> y_ticks{-5.0, 0.0, 5.0};
    const tc_plot_grid_style2d style{0.2f, 0.3f, 0.4f, 0.75f, 2.0f};
    const auto item = tc_plot_grid_item2d_create(
        scene_handle, projection->handle(), x_ticks.data(), x_ticks.size(), y_ticks.data(), y_ticks.size(), style);
    assert(!tc_graphic_item_handle_is_invalid(item));
    assert(tc_visual_scene_item_count(scene_handle) == 1);

    const auto cross_scene_item =
        tc_plot_grid_item2d_create(other_scene_handle, projection->handle(), nullptr, 0, nullptr, 0, style);
    assert(tc_graphic_item_handle_is_invalid(cross_scene_item));

    tc_plot_grid_item_snapshot2d snapshot{};
    assert(tc_plot_grid_item2d_snapshot(scene_handle, item, &snapshot));
    assert(snapshot.x_tick_count == x_ticks.size());
    assert(snapshot.y_tick_count == y_ticks.size());
    assert(snapshot.revision == 1);
    assert(tc_plot_grid_item2d_copy_ticks(scene_handle, item, nullptr, 0, nullptr, 0) ==
           x_ticks.size() + y_ticks.size());
    std::vector<double> copied_x(x_ticks.size());
    std::vector<double> copied_y(y_ticks.size());
    assert(tc_plot_grid_item2d_copy_ticks(
               scene_handle, item, copied_x.data(), copied_x.size(), copied_y.data(), copied_y.size()) ==
           x_ticks.size() + y_ticks.size());
    assert(copied_x == x_ticks && copied_y == y_ticks);

    Resolver resolver;
    tgfx::DrawList2DBuilder builder;
    assert(scene.paint(builder, resolver));
    auto draw_list = builder.freeze();
    assert(draw_list);
    const auto& initial_path = only_path(*draw_list);
    assert(initial_path.stroke);
    assert(initial_path.stroke->width == 2.0f);
    // Two out-of-range X ticks are removed. Three X plus three Y lines remain.
    assert(initial_path.path.verbs().size() == 12);
    const auto initial_bounds = initial_path.path.bounds();
    assert(initial_bounds.x0 >= 25.0f && initial_bounds.x1 <= 395.0f);
    assert(initial_bounds.y0 >= 17.0f && initial_bounds.y1 <= 287.0f);

    assert(projection->update(frame(100.0f, 50.0f, 220.0f, 160.0f, 0.0, 20.0, -10.0, 10.0)));
    builder.clear();
    assert(scene.paint(builder, resolver));
    draw_list = builder.freeze();
    assert(draw_list);
    const auto resized_bounds = only_path(*draw_list).path.bounds();
    assert(resized_bounds.x0 >= 120.0f && resized_bounds.x1 <= 310.0f);
    assert(resized_bounds.y0 >= 60.0f && resized_bounds.y1 <= 190.0f);

    const PlotFrame2D clipped_frame{
        {100.0f, 50.0f, 220.0f, 160.0f},
        {120.0f, 60.0f, 190.0f, 130.0f},
        {0.0, 20.0, -10.0, 10.0},
        {140.0f, 70.0f, 100.0f, 80.0f},
        1.0f,
    };
    assert(projection->update(clipped_frame));
    builder.clear();
    assert(scene.paint(builder, resolver));
    draw_list = builder.freeze();
    assert(draw_list);
    const auto clipped_bounds = only_clip(*draw_list).path.bounds();
    assert(clipped_bounds.x0 == 140.0f && clipped_bounds.x1 == 240.0f);
    assert(clipped_bounds.y0 == 70.0f && clipped_bounds.y1 == 150.0f);

    const double invalid_tick = std::numeric_limits<double>::infinity();
    assert(!tc_plot_grid_item2d_set_ticks(scene_handle, item, &invalid_tick, 1, nullptr, 0));
    assert(!tc_plot_grid_item2d_set_projection(scene_handle, item, foreign_projection->handle()));

    assert(tc_visual_scene_destroy_item(scene_handle, item));
    assert(!tc_plot_grid_item2d_snapshot(scene_handle, item, &snapshot));
    assert(tc_visual_scene_item_count(scene_handle) == 0);

    assert(destroy_plot_projection2d(*projection));
    assert(destroy_plot_projection2d(*foreign_projection));
    tc_visual_scene_destroy(other_scene_handle);
    tc_visual_scene_destroy(scene_handle);
    return 0;
}
