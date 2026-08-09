#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx2/device_factory.hpp>
#include <tgfx2/i_render_device.hpp>
#include <tgfx2/output_transform.hpp>
#include <tgfx2/pipeline_cache.hpp>
#include <tgfx2/render_context.hpp>
#include <tgfx2/tc_shader_bridge.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <system_error>
#include <vector>

namespace {

    bool existing_file(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }

    void configure_shader_artifacts(const char* argv0, const std::filesystem::path& root) {
        const std::array<std::filesystem::path, 3> candidates = {
            argv0 ? std::filesystem::absolute(argv0).parent_path() / "termin_shaderc" : std::filesystem::path{},
            std::filesystem::current_path() / "sdk" / "bin" / "termin_shaderc",
            std::getenv("TERMIN_SDK")
                ? std::filesystem::path(std::getenv("TERMIN_SDK")) / "bin" / "termin_shaderc"
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

    float srgb_oetf(float linear) {
        if (linear <= 0.0031308f)
            return 12.92f * linear;
        return 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    }

    int run_smoke(const char* argv0) {
        constexpr uint32_t kWidth = 64;
        constexpr uint32_t kHeight = 64;
        constexpr float kLinearValue = 0.18f;

        const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path artifact_root =
            std::filesystem::temp_directory_path() / ("termin-output-transform-smoke-" + std::to_string(unique));
        configure_shader_artifacts(argv0, artifact_root);

        std::unique_ptr<tgfx::IRenderDevice> device;
        try {
            device = tgfx::create_device(tgfx::BackendType::Vulkan);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to create Vulkan device: %s\n", e.what());
            return 1;
        }

        tgfx::TextureDesc source_desc;
        source_desc.width = kWidth;
        source_desc.height = kHeight;
        source_desc.format = tgfx::PixelFormat::RGBA32F;
        source_desc.usage = tgfx::TextureUsage::Sampled | tgfx::TextureUsage::CopyDst;
        const tgfx::TextureHandle source = device->create_texture(source_desc);

        tgfx::TextureDesc output_desc;
        output_desc.width = kWidth;
        output_desc.height = kHeight;
        output_desc.format = tgfx::PixelFormat::RGBA8_sRGB;
        output_desc.usage = tgfx::TextureUsage::ColorAttachment | tgfx::TextureUsage::CopySrc;
        const tgfx::TextureHandle output = device->create_texture(output_desc);
        if (!source || !output) {
            std::fprintf(stderr, "Failed to create output-transform smoke textures\n");
            return 1;
        }

        std::vector<float> source_pixels(static_cast<size_t>(kWidth) * kHeight * 4u);
        for (size_t i = 0; i < source_pixels.size(); i += 4) {
            source_pixels[i + 0] = kLinearValue;
            source_pixels[i + 1] = kLinearValue;
            source_pixels[i + 2] = kLinearValue;
            source_pixels[i + 3] = 1.0f;
        }
        device->upload_texture(source,
                               std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(source_pixels.data()),
                                                        source_pixels.size() * sizeof(float)));

        tgfx::PipelineCache cache(*device);
        tgfx::RenderContext2 context(*device, cache);
        tgfx::OutputTransformRenderer renderer;
        context.begin_frame();
        const bool recorded = renderer.record(
            context,
            source,
            output,
            tgfx::OutputTransformParams{
                .sampled_input_encoding = tgfx::TextureEncoding::Linear,
                .target_encoding = tgfx::TextureEncoding::SRGB,
                .dither = tgfx::OutputDitherMode::StableSpatial,
                .target_rgb_bits = 8,
            });
        context.end_frame();
        device->wait_idle();

        std::vector<float> result(source_pixels.size());
        const bool read_ok = recorded && device->read_texture_rgba_float(output, result.data());
        float minimum = 1.0f;
        float maximum = 0.0f;
        double sum = 0.0;
        for (size_t i = 0; i < result.size() && read_ok; i += 4) {
            minimum = std::min(minimum, result[i]);
            maximum = std::max(maximum, result[i]);
            sum += result[i];
        }
        const float mean = static_cast<float>(sum / static_cast<double>(kWidth * kHeight));
        const float expected = srgb_oetf(kLinearValue);
        const bool multiple_codes = maximum - minimum > 0.5f / 255.0f;
        const bool unbiased = std::abs(mean - expected) <= 1.5f / 255.0f;
        std::printf("dithered sRGB: min %.6f, max %.6f, mean %.6f, expected %.6f\n",
                    minimum,
                    maximum,
                    mean,
                    expected);

        // Exercise the OpenXR-shaped path as part of the same smoke: the
        // renderer must compile its array sampler variant and broadcast the
        // fullscreen draw to both attachment layers via multiview.
        source_desc.array_layers = 2;
        output_desc.array_layers = 2;
        const tgfx::TextureHandle multiview_source = device->create_texture(source_desc);
        const tgfx::TextureHandle multiview_output = device->create_texture(output_desc);
        context.begin_frame();
        const bool multiview_recorded = renderer.record(
            context,
            multiview_source,
            multiview_output,
            tgfx::OutputTransformParams{
                .sampled_input_encoding = tgfx::TextureEncoding::Linear,
                .target_encoding = tgfx::TextureEncoding::SRGB,
                .dither = tgfx::OutputDitherMode::StableSpatial,
                .target_rgb_bits = 8,
            });
        context.end_frame();
        device->wait_idle();

        renderer.close();
        device->destroy(multiview_output);
        device->destroy(multiview_source);
        device->destroy(output);
        device->destroy(source);
        std::error_code ec;
        std::filesystem::remove_all(artifact_root, ec);

        if (!read_ok || !multiple_codes || !unbiased || !multiview_recorded) {
            std::fprintf(stderr, "Output-transform dithering contract failed\n");
            return 1;
        }
        return 0;
    }

} // namespace

int main(int argc, char** argv) {
    std::printf("--- tgfx2 output transform pixel smoke ---\n");
    if (!tgfx::backend_is_compiled(tgfx::BackendType::Vulkan)) {
        std::printf("Vulkan backend not compiled, skipping test\n");
        return 0;
    }

    tc_shader_init();
    const int result = run_smoke(argc > 0 ? argv[0] : nullptr);
    tc_shader_shutdown();
    return result;
}
