#include "guard_main.h"

#include <array>

#include <termin/render/render_task.hpp>

namespace {

struct DrawPayload final : termin::RenderTaskExtension {
    std::array<float, 16> model{};
};

} // namespace

TEST_CASE("RenderTaskList keeps task resource packets valid after planning growth") {
    termin::RenderTaskList tasks;
    tasks.reserve(1);

    DrawPayload& payload = tasks.emplace_extension<DrawPayload>();
    payload.model[0] = 42.0f;
    termin::RenderTask& first = tasks.append();
    first.extension = &payload;
    first.final_shader = tc_shader_handle{7};
    first.entity_name = "first";

    const termin::RenderItemNamedUniformBinding uniform{
        "draw_data", payload.model.data(),
        static_cast<uint32_t>(sizeof(payload.model))};
    const termin::RenderItemNamedTextureBinding texture{
        "albedo", tgfx::TextureHandle{9}, tgfx::SamplerHandle{3}};
    first.set_resources(nullptr, std::span{&uniform, size_t{1}},
                        std::span{&texture, size_t{1}});

    for (int i = 0; i < 64; ++i) {
        tasks.append().entity_name = "later";
    }

    REQUIRE(tasks.size() == 65u);
    const termin::RenderTask& stable = *tasks.begin();
    REQUIRE(stable.resources.named_uniform_count == 1u);
    REQUIRE(stable.resources.named_texture_count == 1u);
    CHECK(stable.resources.named_uniforms[0].data == payload.model.data());
    CHECK(stable.resources.named_uniforms[0].size == sizeof(payload.model));
    CHECK(stable.resources.named_textures[0].texture.id == 9u);
    CHECK(stable.resources.named_textures[0].sampler.id == 3u);
    CHECK(stable.final_shader.index == 7u);
    CHECK(stable.entity_name == "first");
}

GUARD_TEST_MAIN();
