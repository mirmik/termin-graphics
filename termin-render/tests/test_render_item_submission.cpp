#include "guard_main.h"

GUARD_TEST_MAIN();

#include <termin/render/render_item_submission.hpp>

namespace {

bool test_encoder(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const termin::RenderItemDrawSubmitRequest& request,
    void* user_data)
{
    (void)ctx;
    (void)item;
    (void)request;
    (void)user_data;
    return true;
}

bool other_test_encoder(
    tgfx::RenderContext2& ctx,
    const tc_render_item& item,
    const termin::RenderItemDrawSubmitRequest& request,
    void* user_data)
{
    (void)ctx;
    (void)item;
    (void)request;
    (void)user_data;
    return true;
}

} // namespace

TEST_CASE("RenderItem draw encoder registry enforces ownership") {
    constexpr uint32_t test_kind = 0x7fff0001u;

    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.debug_name = "test_encoder";

    CHECK(termin::register_render_item_draw_encoder(test_kind, desc));
    CHECK(!termin::register_render_item_draw_encoder(test_kind, desc));
    CHECK(!termin::unregister_render_item_draw_encoder(test_kind, other_test_encoder, nullptr));
    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
    CHECK(!termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
}

TEST_CASE("RenderItem draw encoder registry rejects invalid descriptors") {
    termin::RenderItemDrawEncoderDesc desc{};
    CHECK(!termin::register_render_item_draw_encoder(TC_RENDER_ITEM_KIND_INVALID, desc));

    constexpr uint32_t test_kind = 0x7fff0002u;
    CHECK(!termin::register_render_item_draw_encoder(test_kind, desc));
}

TEST_CASE("RenderItem draw encoder registry reserves built-in mesh encoder") {
    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.debug_name = "replacement_mesh_encoder";

    CHECK(!termin::register_render_item_draw_encoder(TC_RENDER_ITEM_KIND_MESH, desc));
    CHECK(!termin::unregister_render_item_draw_encoder(
        TC_RENDER_ITEM_KIND_MESH,
        test_encoder,
        nullptr));
}

TEST_CASE("RenderItem draw encoder registry exposes built-in mesh capabilities") {
    termin::RenderItemEncoderCapabilities capabilities{};
    REQUIRE(termin::get_render_item_encoder_capabilities(
        TC_RENDER_ITEM_KIND_MESH,
        capabilities));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Color));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Shadow));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Id));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Depth));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::DepthOnly));
    CHECK(termin::render_item_encoder_supports_pass(
        TC_RENDER_ITEM_KIND_MESH,
        termin::RenderItemPassSemantic::Normal));
    CHECK(capabilities.requires_draw_context);
    CHECK(capabilities.consumes_common_resources);
}

TEST_CASE("RenderItem draw encoder registry stores custom capabilities") {
    constexpr uint32_t test_kind = 0x7fff0003u;

    termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr);

    termin::RenderItemDrawEncoderDesc desc{};
    desc.encode = test_encoder;
    desc.debug_name = "capability_test_encoder";
    desc.capabilities.pass_semantic_mask =
        termin::render_item_pass_semantic_bit(termin::RenderItemPassSemantic::Color)
        | termin::render_item_pass_semantic_bit(termin::RenderItemPassSemantic::Shadow);
    desc.capabilities.requires_draw_context = true;
    desc.capabilities.consumes_common_resources = false;

    REQUIRE(termin::register_render_item_draw_encoder(test_kind, desc));

    termin::RenderItemEncoderCapabilities capabilities{};
    REQUIRE(termin::get_render_item_encoder_capabilities(test_kind, capabilities));
    CHECK(termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Color));
    CHECK(termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Shadow));
    CHECK(!termin::render_item_encoder_supports_pass(
        test_kind,
        termin::RenderItemPassSemantic::Depth));
    CHECK(capabilities.requires_draw_context);
    CHECK(!capabilities.consumes_common_resources);

    CHECK(termin::unregister_render_item_draw_encoder(test_kind, test_encoder, nullptr));
    CHECK(!termin::get_render_item_encoder_capabilities(test_kind, capabilities));
}
