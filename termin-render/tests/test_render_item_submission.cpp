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
