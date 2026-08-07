#include "guard_main.h"

GUARD_TEST_MAIN();

#include <array>
#include <string>

#include <termin/render/execute_context.hpp>
#include <termin/render/scene_render_services.hpp>

extern "C" {
#include <tcbase/tc_log.h>
}

namespace {

std::string captured_scene_service_log;

void capture_scene_service_log(tc_log_level level, const char* message)
{
    if (level >= TC_LOG_ERROR && message) {
        captured_scene_service_log = message;
    }
}

} // namespace

TEST_CASE("ExecuteContext is constructible without scene services") {
    termin::ExecuteContext context;
    CHECK(context.scene_services == nullptr);
    CHECK(context.view.primary_view() == nullptr);
}

TEST_CASE("missing SceneRenderServices is an observable error") {
    termin::ExecuteContext context;
    captured_scene_service_log.clear();
    tc_log_set_callback(capture_scene_service_log);
    const auto* services =
        termin::require_scene_render_services(context, "SceneOnlyTestPass");
    tc_log_set_callback(nullptr);

    CHECK(services == nullptr);
    CHECK(captured_scene_service_log.find("SceneOnlyTestPass") != std::string::npos);
    CHECK(captured_scene_service_log.find("no SceneRenderServices") != std::string::npos);
}

TEST_CASE("ExecuteContext builds an ordered MRT pass from independent resources") {
    termin::ExecuteContext context;
    context.tex2_writes.emplace("surface.base", tgfx::TextureHandle{11});
    context.tex2_writes.emplace("surface.normal", tgfx::TextureHandle{22});
    context.tex2_writes.emplace("surface.material", tgfx::TextureHandle{33});
    context.tex2_depth_writes.emplace("surface.depth", tgfx::TextureHandle{44});

    const std::array<termin::FrameGraphColorAttachment, 3> colors{{
        {
            "surface.base",
            tgfx::LoadOp::Clear,
            tgfx::StoreOp::Store,
            {0.1f, 0.2f, 0.3f, 1.0f},
        },
        {
            "surface.normal",
            tgfx::LoadOp::Load,
            tgfx::StoreOp::Store,
            {0.0f, 0.0f, 0.0f, 0.0f},
        },
        {
            "surface.material",
            tgfx::LoadOp::DontCare,
            tgfx::StoreOp::DontCare,
            {0.0f, 0.0f, 0.0f, 0.0f},
        },
    }};
    const termin::FrameGraphDepthAttachment depth{
        "surface.depth",
        tgfx::LoadOp::Clear,
        tgfx::StoreOp::Store,
        0.25f,
        7,
    };

    tgfx::RenderPassDesc pass;
    REQUIRE(context.build_render_pass(colors, &depth, pass));
    REQUIRE_EQ(pass.colors.size(), 3u);
    CHECK_EQ(pass.colors[0].texture.id, 11u);
    CHECK_EQ(pass.colors[1].texture.id, 22u);
    CHECK_EQ(pass.colors[2].texture.id, 33u);
    CHECK(pass.colors[0].load == tgfx::LoadOp::Clear);
    CHECK(pass.colors[1].load == tgfx::LoadOp::Load);
    CHECK(pass.colors[2].store == tgfx::StoreOp::DontCare);
    CHECK_EQ(pass.colors[0].clear_color[2], 0.3f);
    CHECK(pass.has_depth);
    CHECK_EQ(pass.depth.texture.id, 44u);
    CHECK_EQ(pass.depth.clear_depth, 0.25f);
    CHECK_EQ(pass.depth.clear_stencil, 7u);
}

TEST_CASE("ExecuteContext supports a depth-only pass") {
    termin::ExecuteContext context;
    context.tex2_depth_writes.emplace("shadow.depth", tgfx::TextureHandle{55});
    const termin::FrameGraphDepthAttachment depth{
        "shadow.depth",
        tgfx::LoadOp::Load,
        tgfx::StoreOp::DontCare,
        1.0f,
        0,
    };

    tgfx::RenderPassDesc pass;
    REQUIRE(context.build_render_pass({}, &depth, pass));
    CHECK(pass.colors.empty());
    CHECK(pass.has_depth);
    CHECK_EQ(pass.depth.texture.id, 55u);
    CHECK(pass.depth.load == tgfx::LoadOp::Load);
    CHECK(pass.depth.store == tgfx::StoreOp::DontCare);
}

TEST_CASE("ExecuteContext rejects missing named outputs without a partial pass") {
    termin::ExecuteContext context;
    context.tex2_writes.emplace("present", tgfx::TextureHandle{66});
    const std::array<termin::FrameGraphColorAttachment, 2> colors{{
        {"present"},
        {"missing"},
    }};

    tgfx::RenderPassDesc pass;
    REQUIRE(!context.build_render_pass(colors, nullptr, pass));
    CHECK(pass.colors.empty());
    CHECK(!pass.has_depth);
}

TEST_CASE("ExecuteContext rejects duplicate handles in distinct MRT slots") {
    termin::ExecuteContext context;
    context.tex2_writes.emplace("first", tgfx::TextureHandle{77});
    context.tex2_writes.emplace("alias", tgfx::TextureHandle{77});
    const std::array<termin::FrameGraphColorAttachment, 2> colors{{
        {"first"},
        {"alias"},
    }};

    tgfx::RenderPassDesc pass;
    REQUIRE(!context.build_render_pass(colors, nullptr, pass));
    CHECK(pass.colors.empty());
}
