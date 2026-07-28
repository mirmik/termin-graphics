#include "guard_main.h"

GUARD_TEST_MAIN();

#include <array>

#include <termin/render/execute_context.hpp>

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
