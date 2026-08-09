#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/font_atlas.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>
#include <tgfx2/text2d_renderer.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace {

    constexpr uint32_t kWidth = 160;
    constexpr uint32_t kHeight = 48;

    bool existing_file(const std::filesystem::path& path) {
        std::error_code error;
        return std::filesystem::is_regular_file(path, error);
    }

    void configure_shader_artifacts(const char* argv0, const std::filesystem::path& root) {
        const std::array<std::filesystem::path, 3> candidates = {
            argv0 ? std::filesystem::absolute(argv0).parent_path() / "termin_shaderc" : std::filesystem::path{},
            std::filesystem::current_path() / "sdk" / "bin" / "termin_shaderc",
            std::getenv("TERMIN_SDK") ? std::filesystem::path(std::getenv("TERMIN_SDK")) / "bin" / "termin_shaderc"
                                      : std::filesystem::path{},
        };
        for (const auto& candidate : candidates) {
            if (existing_file(candidate)) {
                termin::tgfx2_set_shader_compiler_path(candidate.string().c_str());
                break;
            }
        }
        termin::tgfx2_set_shader_artifact_root(root.string().c_str());
        termin::tgfx2_set_shader_cache_root((root / ".cache").string().c_str());
        termin::tgfx2_set_shader_dev_compile_enabled(true);
    }

    struct TextSample {
        double energy = 0.0;
        uint32_t solid_pixels = 0;
        uint32_t partial_pixels = 0;
    };

    TextSample measure(const std::vector<float>& pixels) {
        TextSample sample;
        for (size_t i = 0; i + 3 < pixels.size(); i += 4) {
            const float value = pixels[i];
            sample.energy += value;
            if (value >= 0.95f) {
                ++sample.solid_pixels;
            } else if (value >= 0.01f) {
                ++sample.partial_pixels;
            }
        }
        return sample;
    }

    bool render_text(tgfx::IRenderDevice& device,
                     tgfx::RenderContext2& context,
                     tgfx::TextureHandle target,
                     tgfx::FontAtlas& font,
                     float size,
                     float bitmap_coverage_gamma,
                     std::optional<float> coverage_gamma,
                     std::vector<float>& pixels) {
        const termin::LinearColor black{0.0f, 0.0f, 0.0f, 1.0f};
        tgfx::Text2DRenderer renderer(&font);
        renderer.set_bitmap_coverage_gamma(bitmap_coverage_gamma);

        context.begin_frame();
        context.begin_pass(target, {}, &black, 1.0f, false);
        context.set_viewport(0, 0, static_cast<int>(kWidth), static_cast<int>(kHeight));
        context.set_depth_test(false);
        context.set_depth_write(false);
        context.set_blend(true);
        context.set_blend_func(tgfx::BlendFactor::SrcAlpha, tgfx::BlendFactor::OneMinusSrcAlpha);
        context.set_cull(tgfx::CullMode::None);
        renderer.begin(&context, static_cast<int>(kWidth), static_cast<int>(kHeight));
        renderer.draw("Small UI text",
                      tgfx::Text2DRenderer::DrawOptions{
                          .x = 4.0f,
                          .y = 4.0f,
                          .color = termin::SrgbColor::white(),
                          .size = size,
                          .coverage_gamma = coverage_gamma,
                      });
        renderer.end();
        context.end_pass();
        context.end_frame();
        device.wait_idle();

        pixels.resize(static_cast<size_t>(kWidth) * kHeight * 4u);
        const bool read = device.read_texture_rgba_float(target, pixels.data());
        renderer.release_gpu();
        return read;
    }

    int run_smoke(const char* argv0) {
        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path artifact_root =
            std::filesystem::temp_directory_path() / ("termin-text-coverage-smoke-" + std::to_string(unique));
        configure_shader_artifacts(argv0, artifact_root);

        std::unique_ptr<tgfx::IRenderDevice> device;
        try {
            device = tgfx::create_device(tgfx::BackendType::Vulkan);
        } catch (const std::exception& error) {
            std::fprintf(stderr, "Failed to create Vulkan device: %s\n", error.what());
            return 1;
        }

        tgfx::TextureDesc target_desc;
        target_desc.width = kWidth;
        target_desc.height = kHeight;
        target_desc.format = tgfx::PixelFormat::RGBA16F;
        target_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
        const tgfx::TextureHandle target = device->create_texture(target_desc);
        if (!target) {
            std::fprintf(stderr, "Failed to create text coverage target\n");
            return 1;
        }

        tgfx::PipelineCache cache(*device);
        tgfx::RenderContext2 context(*device, cache);
        tgfx::FontAtlas font(TGFX2_TEST_FONT_PATH, 14, 512, 512);

        std::vector<float> bitmap_linear;
        std::vector<float> bitmap_corrected;
        const bool bitmap_read =
            render_text(*device, context, target, font, 14.0f, 1.0f, std::nullopt, bitmap_linear) &&
            render_text(*device,
                        context,
                        target,
                        font,
                        14.0f,
                        tgfx::Text2DRenderer::kDefaultBitmapCoverageGamma,
                        std::nullopt,
                        bitmap_corrected);
        const TextSample linear = measure(bitmap_linear);
        const TextSample corrected = measure(bitmap_corrected);

        // Ordinary heading sizes stay on the exact bitmap path, but the
        // small-text weight correction must already have faded to neutral.
        std::vector<float> heading_a;
        std::vector<float> heading_b;
        const bool heading_read = render_text(*device, context, target, font, 28.0f, 1.0f, std::nullopt, heading_a) &&
                                  render_text(*device,
                                              context,
                                              target,
                                              font,
                                              28.0f,
                                              tgfx::Text2DRenderer::kDefaultBitmapCoverageGamma,
                                              std::nullopt,
                                              heading_b);
        float heading_max_delta = 0.0f;
        if (heading_read && heading_a.size() == heading_b.size()) {
            for (size_t i = 0; i < heading_a.size(); ++i) {
                heading_max_delta = std::max(heading_max_delta, std::fabs(heading_a[i] - heading_b[i]));
            }
        }

        // An item-level value is literal: unlike the renderer policy, it does
        // not fade out at heading sizes.
        std::vector<float> heading_overridden;
        const bool heading_override_read = render_text(*device,
                                                       context,
                                                       target,
                                                       font,
                                                       28.0f,
                                                       1.0f,
                                                       tgfx::Text2DRenderer::kDefaultBitmapCoverageGamma,
                                                       heading_overridden);
        const TextSample heading_linear_sample = measure(heading_a);
        const TextSample heading_override_sample = measure(heading_overridden);

        std::vector<float> sdf_a;
        std::vector<float> sdf_b;
        const bool sdf_read = render_text(*device, context, target, font, 52.0f, 1.0f, std::nullopt, sdf_a) &&
                              render_text(*device,
                                          context,
                                          target,
                                          font,
                                          52.0f,
                                          tgfx::Text2DRenderer::kDefaultBitmapCoverageGamma,
                                          std::nullopt,
                                          sdf_b);
        float sdf_max_delta = 0.0f;
        if (sdf_read && sdf_a.size() == sdf_b.size()) {
            for (size_t i = 0; i < sdf_a.size(); ++i) {
                sdf_max_delta = std::max(sdf_max_delta, std::fabs(sdf_a[i] - sdf_b[i]));
            }
        }

        std::printf("bitmap coverage: linear energy %.3f, corrected %.3f, solid %u/%u, partial %u/%u\n",
                    linear.energy,
                    corrected.energy,
                    linear.solid_pixels,
                    corrected.solid_pixels,
                    linear.partial_pixels,
                    corrected.partial_pixels);
        std::printf(
            "heading bitmap control max delta: %.7f, SDF control max delta: %.7f\n", heading_max_delta, sdf_max_delta);

        const bool bitmap_contract = bitmap_read && linear.partial_pixels > 0 && corrected.energy < linear.energy &&
                                     corrected.energy > linear.energy * 0.70 &&
                                     corrected.solid_pixels + 2 >= linear.solid_pixels;
        const bool heading_neutral = heading_read && heading_max_delta <= 1.0e-6f;
        const bool heading_override_literal =
            heading_override_read && heading_override_sample.energy < heading_linear_sample.energy;
        const bool sdf_unchanged = sdf_read && sdf_max_delta <= 1.0e-6f;

        font.release_gpu();
        device->destroy(target);
        std::error_code error;
        std::filesystem::remove_all(artifact_root, error);
        if (!bitmap_contract || !heading_neutral || !heading_override_literal || !sdf_unchanged) {
            std::fprintf(stderr,
                         "Text coverage contract failed: bitmap=%d heading=%d override=%d sdf=%d\n",
                         bitmap_contract,
                         heading_neutral,
                         heading_override_literal,
                         sdf_unchanged);
            return 1;
        }
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    std::printf("--- tgfx2 text coverage pixel smoke ---\n");
    if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        std::printf("Vulkan backend not compiled, skipping test\n");
        return 0;
    }

    tc_shader_init();
    const int result = run_smoke(argc > 0 ? argv[0] : nullptr);
    tc_shader_shutdown();
    return result;
}
