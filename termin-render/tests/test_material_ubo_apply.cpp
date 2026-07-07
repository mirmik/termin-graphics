#include "guard_main.h"

GUARD_TEST_MAIN();

#include <array>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include <termin/render/material_ubo_apply.hpp>

namespace {

int32_t read_int_at(const std::array<uint8_t, 16>& buffer, size_t offset)
{
    int32_t value = 0;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

tc_uniform_value uniform_int(const char* name, int32_t value)
{
    tc_uniform_value uniform{};
    std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
    uniform.type = TC_UNIFORM_INT;
    uniform.data.i = value;
    return uniform;
}

tc_uniform_value uniform_bool(const char* name, bool value)
{
    tc_uniform_value uniform{};
    std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
    uniform.type = TC_UNIFORM_BOOL;
    uniform.data.i = value ? 1 : 0;
    return uniform;
}

tc_uniform_value uniform_float(const char* name, float value)
{
    tc_uniform_value uniform{};
    std::snprintf(uniform.name, sizeof(uniform.name), "%s", name);
    uniform.type = TC_UNIFORM_FLOAT;
    uniform.data.f = value;
    return uniform;
}

} // namespace

TEST_CASE("material UBO Bool field packs Bool uniforms as int32")
{
    std::array<uint8_t, 16> buffer{};
    const tc_uniform_value enabled = uniform_bool("u_enabled", true);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(
        enabled,
        "Bool",
        buffer.data()));

    CHECK_EQ(read_int_at(buffer, 0), 1);
}

TEST_CASE("material UBO Bool field accepts Int uniforms as 0 or 1")
{
    std::array<uint8_t, 16> buffer{};
    tc_uniform_value disabled = uniform_int("u_disabled", 0);
    tc_uniform_value enabled = uniform_int("u_enabled", 42);

    REQUIRE(termin::pack_material_uniform_value_to_std140_field(
        disabled,
        "Bool",
        buffer.data()));
    REQUIRE(termin::pack_material_uniform_value_to_std140_field(
        enabled,
        "Bool",
        buffer.data() + 4));

    CHECK_EQ(read_int_at(buffer, 0), 0);
    CHECK_EQ(read_int_at(buffer, 4), 1);
}

TEST_CASE("material UBO Bool field rejects non-integral uniforms")
{
    std::array<uint8_t, 16> buffer{};
    buffer[0] = 0xCD;
    buffer[1] = 0xCD;
    buffer[2] = 0xCD;
    buffer[3] = 0xCD;
    const tc_uniform_value value = uniform_float("u_enabled", 1.0f);

    CHECK(!termin::pack_material_uniform_value_to_std140_field(
        value,
        "Bool",
        buffer.data()));

    CHECK_EQ(buffer[0], 0xCD);
    CHECK_EQ(buffer[1], 0xCD);
    CHECK_EQ(buffer[2], 0xCD);
    CHECK_EQ(buffer[3], 0xCD);
}
