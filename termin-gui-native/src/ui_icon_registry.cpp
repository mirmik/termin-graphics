#include <termin/gui_native/ui_icon_registry.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <vector>

#include <tgfx2/path2d.hpp>

namespace termin::gui_native {
    namespace {
        constexpr float kViewBoxExtent = 24.0f;
        constexpr uint32_t kSupersample = 8;
        constexpr uint32_t kMaxRasterExtent = 256;

        struct IconRecipe {
            std::vector<tgfx::Path2f> strokes;
        };

        using RecipeBuilder = IconRecipe (*)();

        struct IconEntry {
            std::string_view id;
            RecipeBuilder build;
        };

        tgfx::Path2f line_path(std::initializer_list<termin::Vec2f> points) {
            tgfx::Path2f path;
            auto point = points.begin();
            if (point == points.end())
                return path;
            path.move_to(*point++);
            for (; point != points.end(); ++point)
                path.line_to(*point);
            return path;
        }

        IconRecipe build_add() {
            IconRecipe recipe;
            recipe.strokes.push_back(line_path({{12.0f, 5.0f}, {12.0f, 19.0f}}));
            recipe.strokes.push_back(line_path({{5.0f, 12.0f}, {19.0f, 12.0f}}));
            return recipe;
        }

        IconRecipe build_collapse_all() {
            IconRecipe recipe;
            recipe.strokes.push_back(line_path({{7.0f, 4.5f}, {12.0f, 9.0f}, {17.0f, 4.5f}}));
            recipe.strokes.push_back(line_path({{7.0f, 19.5f}, {12.0f, 15.0f}, {17.0f, 19.5f}}));
            return recipe;
        }

        IconRecipe build_refresh() {
            constexpr float kTau = 6.2831853071795864769f;
            constexpr float kSweep = kTau * 0.875f;
            constexpr int kSegments = 40;
            constexpr float kRadius = 8.5f;

            tgfx::Path2f arc;
            arc.move_to({12.0f + kRadius, 12.0f});
            for (int segment = 1; segment <= kSegments; ++segment) {
                const float angle = kSweep * static_cast<float>(segment) / static_cast<float>(kSegments);
                arc.line_to({12.0f + std::cos(angle) * kRadius, 12.0f + std::sin(angle) * kRadius});
            }
            arc.line_to({21.0f, 8.0f});

            IconRecipe recipe;
            recipe.strokes.push_back(std::move(arc));
            recipe.strokes.push_back(line_path({{21.0f, 3.0f}, {21.0f, 8.0f}, {16.0f, 8.0f}}));
            return recipe;
        }

        constexpr std::array kIcons{
            IconEntry{"add", &build_add},
            IconEntry{"collapse-all", &build_collapse_all},
            IconEntry{"refresh", &build_refresh},
        };

        const IconEntry* find_icon(std::string_view icon_id) noexcept {
            const auto found = std::find_if(
                kIcons.begin(), kIcons.end(), [icon_id](const IconEntry& entry) { return entry.id == icon_id; });
            return found == kIcons.end() ? nullptr : &*found;
        }
    } // namespace

    const UiIconRegistry& UiIconRegistry::builtin() {
        static const UiIconRegistry registry;
        return registry;
    }

    bool UiIconRegistry::contains(std::string_view icon_id) const noexcept {
        return find_icon(icon_id) != nullptr;
    }

    bool UiIconRegistry::paint(tc_ui_paint_context* context,
                               std::string_view icon_id,
                               tc_ui_rect rect,
                               tc_ui_srgb_color color) const {
        const IconEntry* entry = find_icon(icon_id);
        if (!context || !entry || rect.width <= 0.0f || rect.height <= 0.0f)
            return false;
        tc_ui_painter_draw_icon(context, entry->id.data(), rect, color);
        return true;
    }

    std::vector<uint8_t>
    UiIconRegistry::rasterize(std::string_view icon_id, uint32_t width, uint32_t height) const {
        const IconEntry* entry = find_icon(icon_id);
        if (!entry || width == 0 || height == 0 || width > kMaxRasterExtent || height > kMaxRasterExtent)
            return {};

        const IconRecipe recipe = entry->build();
        std::vector<tgfx::FlattenedPath2f> strokes;
        strokes.reserve(recipe.strokes.size());
        for (const tgfx::Path2f& path : recipe.strokes)
            strokes.push_back(path.flatten(0.05f));

        tgfx::StrokePaint stroke;
        stroke.width = 2.0f;
        stroke.join = tgfx::StrokeJoin::Round;
        stroke.cap = tgfx::StrokeCap::Round;

        const float extent = static_cast<float>(std::min(width, height));
        const float offset_x = (static_cast<float>(width) - extent) * 0.5f;
        const float offset_y = (static_cast<float>(height) - extent) * 0.5f;
        const float samples_per_pixel = static_cast<float>(kSupersample * kSupersample);
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4, 255);

        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint32_t covered = 0;
                for (uint32_t sample_y = 0; sample_y < kSupersample; ++sample_y) {
                    for (uint32_t sample_x = 0; sample_x < kSupersample; ++sample_x) {
                        const float physical_x =
                            static_cast<float>(x) + (static_cast<float>(sample_x) + 0.5f) / kSupersample;
                        const float physical_y =
                            static_cast<float>(y) + (static_cast<float>(sample_y) + 0.5f) / kSupersample;
                        const termin::Vec2f point{
                            (physical_x - offset_x) * kViewBoxExtent / extent,
                            (physical_y - offset_y) * kViewBoxExtent / extent,
                        };
                        const bool hit = std::any_of(strokes.begin(), strokes.end(), [&](const auto& path) {
                            return path.stroke_contains(point, stroke);
                        });
                        covered += hit ? 1u : 0u;
                    }
                }
                const size_t pixel = (static_cast<size_t>(y) * width + x) * 4;
                rgba[pixel + 3] = static_cast<uint8_t>(
                    std::lround(static_cast<float>(covered) * 255.0f / samples_per_pixel));
            }
        }
        return rgba;
    }

} // namespace termin::gui_native
