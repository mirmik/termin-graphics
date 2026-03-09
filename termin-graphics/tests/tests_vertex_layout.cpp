#include "guard/guard.h"

extern "C" {
#include "tgfx/tgfx_types.h"
}

#include <cstring>

TEST_CASE("attrib_type_size returns correct sizes") {
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_FLOAT32), 4u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_INT32), 4u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_UINT32), 4u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_INT16), 2u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_UINT16), 2u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_INT8), 1u);
    CHECK_EQ(tgfx_attrib_type_size(TGFX_ATTRIB_UINT8), 1u);
}

TEST_CASE("vertex_layout_init zeroes the layout") {
    tgfx_vertex_layout layout;
    memset(&layout, 0xFF, sizeof(layout));
    tgfx_vertex_layout_init(&layout);

    CHECK_EQ(layout.stride, 0);
    CHECK_EQ(layout.attrib_count, 0);
}

TEST_CASE("vertex_layout_add single attribute") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);

    bool ok = tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    CHECK(ok);
    CHECK_EQ(layout.attrib_count, 1);
    CHECK_EQ(layout.stride, 12);  // 3 * 4 bytes

    CHECK_EQ(layout.attribs[0].size, 3);
    CHECK_EQ(layout.attribs[0].type, (uint8_t)TGFX_ATTRIB_FLOAT32);
    CHECK_EQ(layout.attribs[0].location, 0);
    CHECK_EQ(layout.attribs[0].offset, 0);
    CHECK_EQ(strcmp(layout.attribs[0].name, "position"), 0);
}

TEST_CASE("vertex_layout_add multiple attributes compute correct offsets") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);

    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);

    CHECK_EQ(layout.attrib_count, 3);
    CHECK_EQ(layout.stride, 32);  // (3+3+2) * 4

    CHECK_EQ(layout.attribs[0].offset, 0);   // position at 0
    CHECK_EQ(layout.attribs[1].offset, 12);  // normal at 12
    CHECK_EQ(layout.attribs[2].offset, 24);  // uv at 24
}

TEST_CASE("vertex_layout_add mixed types compute correct stride") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);

    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "joints", 4, TGFX_ATTRIB_UINT16, 3);
    tgfx_vertex_layout_add(&layout, "weights", 4, TGFX_ATTRIB_UINT8, 4);

    CHECK_EQ(layout.attrib_count, 3);
    // 3*4 + 4*2 + 4*1 = 12 + 8 + 4 = 24
    CHECK_EQ(layout.stride, 24);

    CHECK_EQ(layout.attribs[0].offset, 0);
    CHECK_EQ(layout.attribs[1].offset, 12);
    CHECK_EQ(layout.attribs[2].offset, 20);
}

TEST_CASE("vertex_layout_add rejects when max attribs reached") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);

    for (int i = 0; i < TGFX_VERTEX_ATTRIBS_MAX; i++) {
        char name[32];
        snprintf(name, sizeof(name), "attr%d", i);
        CHECK(tgfx_vertex_layout_add(&layout, name, 1, TGFX_ATTRIB_FLOAT32, i));
    }

    CHECK_EQ(layout.attrib_count, TGFX_VERTEX_ATTRIBS_MAX);
    CHECK_FALSE(tgfx_vertex_layout_add(&layout, "overflow", 1, TGFX_ATTRIB_FLOAT32, 9));
}

TEST_CASE("vertex_layout_add rejects null inputs") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);

    CHECK_FALSE(tgfx_vertex_layout_add(nullptr, "pos", 3, TGFX_ATTRIB_FLOAT32, 0));
    CHECK_FALSE(tgfx_vertex_layout_add(&layout, nullptr, 3, TGFX_ATTRIB_FLOAT32, 0));
    CHECK_EQ(layout.attrib_count, 0);
}

TEST_CASE("vertex_layout_find finds existing attribute") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);
    tgfx_vertex_layout_add(&layout, "normal", 3, TGFX_ATTRIB_FLOAT32, 1);
    tgfx_vertex_layout_add(&layout, "uv", 2, TGFX_ATTRIB_FLOAT32, 2);

    const tgfx_vertex_attrib* pos = tgfx_vertex_layout_find(&layout, "position");
    REQUIRE(pos != nullptr);
    CHECK_EQ(pos->location, 0);
    CHECK_EQ(pos->size, 3);

    const tgfx_vertex_attrib* uv = tgfx_vertex_layout_find(&layout, "uv");
    REQUIRE(uv != nullptr);
    CHECK_EQ(uv->location, 2);
    CHECK_EQ(uv->size, 2);
    CHECK_EQ(uv->offset, 24);
}

TEST_CASE("vertex_layout_find returns null for missing attribute") {
    tgfx_vertex_layout layout;
    tgfx_vertex_layout_init(&layout);
    tgfx_vertex_layout_add(&layout, "position", 3, TGFX_ATTRIB_FLOAT32, 0);

    CHECK(tgfx_vertex_layout_find(&layout, "color") == nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, "") == nullptr);
    CHECK(tgfx_vertex_layout_find(nullptr, "position") == nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, nullptr) == nullptr);
}

TEST_CASE("predefined layout pos") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_pos();
    CHECK_EQ(layout.attrib_count, 1);
    CHECK_EQ(layout.stride, 12);
    CHECK(tgfx_vertex_layout_find(&layout, "position") != nullptr);
}

TEST_CASE("predefined layout pos_normal") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_pos_normal();
    CHECK_EQ(layout.attrib_count, 2);
    CHECK_EQ(layout.stride, 24);
    CHECK(tgfx_vertex_layout_find(&layout, "position") != nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, "normal") != nullptr);
}

TEST_CASE("predefined layout pos_normal_uv") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_pos_normal_uv();
    CHECK_EQ(layout.attrib_count, 3);
    CHECK_EQ(layout.stride, 32);
    CHECK(tgfx_vertex_layout_find(&layout, "position") != nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, "normal") != nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, "uv") != nullptr);
}

TEST_CASE("predefined layout pos_normal_uv_tangent") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_pos_normal_uv_tangent();
    CHECK_EQ(layout.attrib_count, 4);
    // (3+3+2+4) * 4 = 48
    CHECK_EQ(layout.stride, 48);
    CHECK(tgfx_vertex_layout_find(&layout, "tangent") != nullptr);
}

TEST_CASE("predefined layout skinned") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_skinned();
    CHECK_EQ(layout.attrib_count, 5);
    // (3+3+2+4+4) * 4 = 64
    CHECK_EQ(layout.stride, 64);
    CHECK(tgfx_vertex_layout_find(&layout, "joints") != nullptr);
    CHECK(tgfx_vertex_layout_find(&layout, "weights") != nullptr);
}

TEST_CASE("predefined layout locations are sequential") {
    tgfx_vertex_layout layout = tgfx_vertex_layout_pos_normal_uv();
    CHECK_EQ(layout.attribs[0].location, 0);
    CHECK_EQ(layout.attribs[1].location, 1);
    CHECK_EQ(layout.attribs[2].location, 2);
}
