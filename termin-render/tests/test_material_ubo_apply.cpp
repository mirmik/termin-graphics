#include "guard_main.h"

GUARD_TEST_MAIN();

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <tcbase/tc_log.h>
#include <termin/render/material_ubo_apply.hpp>

namespace {

    std::string captured_log;

    void capture_log(tc_log_level level, const char* message) {
        if (level == TC_LOG_ERROR && message) {
            captured_log = message;
        }
    }

    int32_t read_int_at(const std::array<uint8_t, 16>& buffer, size_t offset) {
        int32_t value = 0;
        std::memcpy(&value, buffer.data() + offset, sizeof(value));
        return value;
    }

    float read_float_at(const std::array<uint8_t, 16>& buffer, size_t offset) {
        float value = 0.0f;
        std::memcpy(&value, buffer.data() + offset, sizeof(value));
        return value;
    }

    tc_uniform_value uniform_int(const char* name, int32_t value) {
        tc_uniform_value uniform{};
        std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
        uniform.type = TC_UNIFORM_INT;
        uniform.data.i = value;
        return uniform;
    }

    tc_uniform_value uniform_bool(const char* name, bool value) {
        tc_uniform_value uniform{};
        std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
        uniform.type = TC_UNIFORM_BOOL;
        uniform.data.i = value ? 1 : 0;
        return uniform;
    }

    tc_uniform_value uniform_float(const char* name, float value) {
        tc_uniform_value uniform{};
        std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
        uniform.type = TC_UNIFORM_FLOAT;
        uniform.data.f = value;
        return uniform;
    }

    tc_uniform_value uniform_color(const char* name, uint8_t type, float r, float g, float b, float a) {
        tc_uniform_value uniform{};
        std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
        uniform.type = type;
        if (type == TC_UNIFORM_SRGB_COLOR) {
            uniform.data.srgb_color = {r, g, b, a};
        } else if (type == TC_UNIFORM_LINEAR_COLOR) {
            uniform.data.linear_color = {r, g, b, a};
        } else {
            uniform.data.v4[0] = r;
            uniform.data.v4[1] = g;
            uniform.data.v4[2] = b;
            uniform.data.v4[3] = a;
        }
        return uniform;
    }

} // namespace

TEST_CASE("material UBO Bool field packs Bool uniforms as int32") {
    std::array<uint8_t, 16> buffer{};
    const tc_uniform_value enabled = uniform_bool("u_enabled", true);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(enabled, "Bool", buffer.data()));

    CHECK_EQ(read_int_at(buffer, 0), 1);
}

TEST_CASE("material UBO Bool field accepts Int uniforms as 0 or 1") {
    std::array<uint8_t, 16> buffer{};
    tc_uniform_value disabled = uniform_int("u_disabled", 0);
    tc_uniform_value enabled = uniform_int("u_enabled", 42);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(disabled, "Bool", buffer.data()));
    REQUIRE(termin::pack_material_uniform_value_to_std140_field(enabled, "Bool", buffer.data() + 4));

    CHECK_EQ(read_int_at(buffer, 0), 0);
    CHECK_EQ(read_int_at(buffer, 4), 1);
}

TEST_CASE("material UBO Bool field rejects non-integral uniforms") {
    std::array<uint8_t, 16> buffer{};
    buffer[0] = 0xCD;
    buffer[1] = 0xCD;
    buffer[2] = 0xCD;
    buffer[3] = 0xCD;
    const tc_uniform_value value = uniform_float("u_enabled", 1.0f);

    captured_log.clear();
    tc_log_set_callback(capture_log);
    const bool packed = termin::pack_material_uniform_value_to_std140_field(value, "Bool", buffer.data());
    tc_log_set_callback(nullptr);

    CHECK(!packed);

    CHECK_EQ(buffer[0], 0xCD);
    CHECK_EQ(buffer[1], 0xCD);
    CHECK_EQ(buffer[2], 0xCD);
    CHECK_EQ(buffer[3], 0xCD);
    CHECK(captured_log.find("u_enabled") != std::string::npos);
    CHECK(captured_log.find("Float") != std::string::npos);
    CHECK(captured_log.find("Bool") != std::string::npos);
}

TEST_CASE("material UBO authored #808080 decodes once and preserves alpha") {
    std::array<uint8_t, 16> buffer{};
    constexpr float authored = 128.0f / 255.0f;
    const tc_uniform_value value = uniform_color("u_color", TC_UNIFORM_SRGB_COLOR, authored, authored, authored, 0.5f);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(value, "SrgbColor", buffer.data()));

    CHECK(std::fabs(read_float_at(buffer, 0) - 0.2158605f) < 1.0e-5f);
    CHECK(std::fabs(read_float_at(buffer, 4) - 0.2158605f) < 1.0e-5f);
    CHECK(std::fabs(read_float_at(buffer, 8) - 0.2158605f) < 1.0e-5f);
    CHECK_EQ(read_float_at(buffer, 12), 0.5f);
}

TEST_CASE("material UBO reflected Vec4 preserves authored color semantics") {
    std::array<uint8_t, 16> buffer{};
    constexpr float authored = 128.0f / 255.0f;
    const tc_uniform_value srgb =
        uniform_color("u_srgb", TC_UNIFORM_SRGB_COLOR, authored, authored, authored, 0.25f);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(srgb, "Vec4", buffer.data()));
    CHECK(std::fabs(read_float_at(buffer, 0) - 0.2158605f) < 1.0e-5f);
    CHECK(std::fabs(read_float_at(buffer, 4) - 0.2158605f) < 1.0e-5f);
    CHECK(std::fabs(read_float_at(buffer, 8) - 0.2158605f) < 1.0e-5f);
    CHECK_EQ(read_float_at(buffer, 12), 0.25f);

    const tc_uniform_value linear =
        uniform_color("u_linear", TC_UNIFORM_LINEAR_COLOR, 4.0f, 0.5f, -2.0f, 0.75f);
    REQUIRE(termin::pack_material_uniform_value_to_std140_field(linear, "Vec4", buffer.data()));
    CHECK_EQ(read_float_at(buffer, 0), 4.0f);
    CHECK_EQ(read_float_at(buffer, 4), 0.5f);
    CHECK_EQ(read_float_at(buffer, 8), -2.0f);
    CHECK_EQ(read_float_at(buffer, 12), 0.75f);
}

TEST_CASE("material UBO LinearColor and Vec4 preserve literal values including HDR") {
    std::array<uint8_t, 16> buffer{};
    const tc_uniform_value linear = uniform_color("u_linear", TC_UNIFORM_LINEAR_COLOR, 4.0f, 0.5f, -2.0f, 0.5f);
    const tc_uniform_value vec4 = uniform_color("u_vec4", TC_UNIFORM_VEC4, 0.5f, 0.5f, 0.5f, 0.5f);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(linear, "LinearColor", buffer.data()));
    CHECK_EQ(read_float_at(buffer, 0), 4.0f);
    CHECK_EQ(read_float_at(buffer, 4), 0.5f);
    CHECK_EQ(read_float_at(buffer, 8), -2.0f);
    CHECK_EQ(read_float_at(buffer, 12), 0.5f);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(vec4, "Vec4", buffer.data()));
    CHECK_EQ(read_float_at(buffer, 0), 0.5f);
}

TEST_CASE("material UBO color fields reject cross-kind and legacy Color values") {
    std::array<uint8_t, 16> buffer{};
    buffer.fill(0xCD);
    const tc_uniform_value srgb = uniform_color("u_color", TC_UNIFORM_SRGB_COLOR, 0.5f, 0.5f, 0.5f, 0.5f);
    const tc_uniform_value linear = uniform_color("u_color", TC_UNIFORM_LINEAR_COLOR, 0.5f, 0.5f, 0.5f, 0.5f);
    const tc_uniform_value vec4 = uniform_color("u_color", TC_UNIFORM_VEC4, 0.5f, 0.5f, 0.5f, 0.5f);

    captured_log.clear();
    tc_log_set_callback(capture_log);
    CHECK(!termin::pack_material_uniform_value_to_std140_field(srgb, "LinearColor", buffer.data()));
    CHECK(!termin::pack_material_uniform_value_to_std140_field(linear, "SrgbColor", buffer.data()));
    CHECK(!termin::pack_material_uniform_value_to_std140_field(vec4, "Color", buffer.data()));
    tc_log_set_callback(nullptr);

    CHECK_EQ(buffer[0], 0xCD);
    CHECK(captured_log.find("u_color") != std::string::npos);
    CHECK(captured_log.find("incompatible") != std::string::npos);
}
