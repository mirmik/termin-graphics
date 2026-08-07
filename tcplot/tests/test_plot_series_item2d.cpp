#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <chrono>
#include <cstdio>
#include <variant>
#include <vector>

#include <termin_visual_scene/scene_render2d.hpp>

#include "tcplot/plot_series_item2d.hpp"

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

    tcplot::PlotFrame2D frame() {
        return {
            {0.0f, 0.0f, 640.0f, 480.0f},
            {80.0f, 40.0f, 540.0f, 380.0f},
            {0.0, 10.0, -5.0, 5.0},
            {100.0f, 60.0f, 480.0f, 320.0f},
            1.0f,
        };
    }

    std::size_t retained_count(const tgfx::DrawList2D& list) {
        std::size_t result = 0;
        for (const auto& command : list.commands()) {
            if (std::holds_alternative<tgfx::DrawRetainedBatch2D>(command)) {
                ++result;
            }
        }
        return result;
    }

} // namespace

int main() {
    using termin::visual::TcVisualScene;

    const auto scene_handle = tc_visual_scene_create();
    const auto other_scene_handle = tc_visual_scene_create();
    TcVisualScene scene{scene_handle};
    TcVisualScene other_scene{other_scene_handle};
    const auto projection = tcplot::create_plot_projection2d(scene, frame());
    const auto other_projection = tcplot::create_plot_projection2d(other_scene, frame());
    assert(projection && other_projection);

    std::vector<double> x{0.0, 2.0, 5.0, 8.0, 10.0};
    std::vector<double> y{-5.0, -2.0, 0.0, 3.0, 5.0};
    const tc_plot_line_style_state2d line_style{
        {0.2f, 0.6f, 0.9f, 0.8f},
        2.0f,
        TC_PLOT_LINE_STYLE_SOLID_2D,
        8.0f,
        5.0f,
        TC_PLOT_COLORMAP_SOLID_2D,
        false,
        0.0,
        1.0,
    };
    const tc_plot_scatter_style_state2d scatter_style{
        {0.9f, 0.4f, 0.2f, 1.0f},
        7.0f,
    };
    const auto line = tc_plot_line_series_item2d_create(
        scene_handle, projection->handle(), x.data(), y.data(), nullptr, x.size(), line_style);
    const auto scatter = tc_plot_scatter_series_item2d_create(
        scene_handle, projection->handle(), x.data(), y.data(), x.size(), scatter_style);
    assert(!tc_graphic_item_handle_is_invalid(line));
    assert(!tc_graphic_item_handle_is_invalid(scatter));
    assert(tc_visual_scene_item_count(scene_handle) == 2);
    assert(tc_graphic_item_handle_is_invalid(tc_plot_line_series_item2d_create(
        other_scene_handle, projection->handle(), x.data(), y.data(), nullptr, x.size(), line_style)));

    tc_plot_series_snapshot2d snapshot{};
    tc_plot_line_style_state2d copied_line_style{};
    assert(tc_plot_line_series_item2d_snapshot(scene_handle, line, &snapshot, &copied_line_style));
    assert(snapshot.point_count == x.size());
    assert(!snapshot.has_scalar && snapshot.revision == 1);
    assert(copied_line_style.thickness_px == 2.0f);

    const double append_x[] = {11.0, 12.0};
    const double append_y[] = {4.0, 3.0};
    assert(tc_plot_line_series_item2d_append(scene_handle, line, append_x, append_y, nullptr, 2));
    assert(tc_plot_line_series_item2d_snapshot(scene_handle, line, &snapshot, &copied_line_style));
    assert(snapshot.point_count == 7 && snapshot.revision == 2);
    std::vector<double> copied_x(7);
    std::vector<double> copied_y(7);
    assert(tc_plot_line_series_item2d_copy_data(
               scene_handle, line, copied_x.data(), copied_y.data(), nullptr, copied_x.size()) == copied_x.size());
    assert(copied_x.back() == 12.0 && copied_y.back() == 3.0);

    tc_plot_nearest_point2d nearest{};
    assert(tc_plot_line_series_item2d_nearest(scene_handle, line, 350.0f, 230.0f, 2.0f, &nearest));
    assert(nearest.index == 2 && nearest.data_x == 5.0);
    assert(!tc_plot_line_series_item2d_set_projection(scene_handle, line, other_projection->handle()));

    auto* line_item = scene.resolve(line);
    auto* scatter_item = scene.resolve(scatter);
    assert(line_item && scatter_item);
    line_item->z_order = 20;
    scatter_item->z_order = 10;
    line_item->opacity = 0.5f;
    scatter_item->visible = false;

    Resolver resolver;
    tgfx::DrawList2DBuilder builder;
    assert(scene.paint(builder, resolver));
    auto list = builder.freeze();
    assert(list && retained_count(*list) == 1);
    scatter_item->visible = true;
    builder.clear();
    assert(scene.paint(builder, resolver));
    list = builder.freeze();
    assert(list && retained_count(*list) == 2);

    // A retained scene snapshot owns only two small shared commands regardless
    // of point count. Painting 200k points therefore performs no O(N) copy.
    constexpr std::size_t large_count = 200000;
    std::vector<double> large_x(large_count);
    std::vector<double> large_y(large_count);
    for (std::size_t index = 0; index < large_count; ++index) {
        large_x[index] = static_cast<double>(index) / 1000.0;
        large_y[index] = static_cast<double>(index % 1000) / 100.0;
    }
    assert(
        tc_plot_line_series_item2d_set_data(scene_handle, line, large_x.data(), large_y.data(), nullptr, large_count));
    const auto started = std::chrono::steady_clock::now();
    std::size_t command_count = 0;
    for (int iteration = 0; iteration < 25; ++iteration) {
        builder.clear();
        assert(scene.paint(builder, resolver));
        auto measured = builder.freeze();
        assert(measured && retained_count(*measured) == 2);
        if (iteration == 0)
            command_count = measured->size();
        assert(measured->size() == command_count);
    }
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
    std::printf("retained-series paint: 200000 points, 25 snapshots, %lld us, "
                "%zu commands\n",
                static_cast<long long>(elapsed.count()),
                command_count);
    assert(elapsed < std::chrono::milliseconds(500));

    // Frozen commands keep their renderer state alive across scene teardown;
    // GPU resources are consequently released by the last command/state owner.
    auto retained_list = std::move(*list);
    assert(tc_visual_scene_destroy_item(scene_handle, line));
    assert(tc_visual_scene_destroy_item(scene_handle, scatter));
    assert(retained_count(retained_list) == 2);
    assert(!tc_plot_line_series_item2d_snapshot(scene_handle, line, &snapshot, &copied_line_style));

    assert(tcplot::destroy_plot_projection2d(*projection));
    assert(tcplot::destroy_plot_projection2d(*other_projection));
    tc_visual_scene_destroy(other_scene_handle);
    tc_visual_scene_destroy(scene_handle);
    return 0;
}
