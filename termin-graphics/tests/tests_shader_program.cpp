#include "guard_main.h"

#include <cstring>
#include <string>

#include <tgfx/tgfx_shader_handle.hpp>
#include <tgfx/tgfx_shader_program_handle.hpp>

using termin::TcShader;
using termin::TcShaderProgram;

TEST_CASE("shader program registry owns canonical payload and phase shaders") {
    tc_shader_init();
    tc_shader_program_init();

    tc_shader_program_handle stale = tc_shader_program_handle_invalid();
    tc_shader_handle old_shader_handle = tc_shader_handle_invalid();
    std::string old_shader_uuid;
    {
        TcShaderProgram program = TcShaderProgram::declare("program-test-uuid", "Program Test");
        REQUIRE(program.is_valid());
        stale = program.handle;
        CHECK_EQ(std::string(program.uuid()), "program-test-uuid");
        CHECK_EQ(program.version(), 1u);

        tc_uniform_value default_value{};
        default_value.type = TC_UNIFORM_FLOAT;
        default_value.data.f = 0.5f;
        const tc_shader_program_property_desc property = {
            "roughness", "Float", "Roughness", &default_value, 0.0, 1.0, 1, 1};
        const tc_shader_program_phase_desc phases[] = {
            {"opaque", 3, tc_render_state_opaque()},
            {"shadow", -1, tc_render_state_opaque()},
        };
        const tc_shader_program_payload_desc payload = {
            "Program Test",
            "materials/program.shader",
            "slang",
            7,
            &property,
            1,
            phases,
            2,
        };
        REQUIRE(tc_shader_program_set_payload(program.get(), &payload));
        CHECK_EQ(program.version(), 2u);
        CHECK_EQ(program.get()->property_count, 1u);
        CHECK_EQ(std::string(program.get()->properties[0].name), "roughness");
        CHECK_EQ(program.get()->properties[0].default_value.data.f, 0.5f);
        CHECK_EQ(program.get()->phase_count, 2u);
        CHECK_EQ(std::string(program.get()->phases[0].phase_mark), "opaque");

        const tc_shader_program_phase* opaque =
            tc_shader_program_find_phase(program.get(), "opaque");
        REQUIRE(opaque != nullptr);
        old_shader_handle = opaque->shader;
        TcShader old_shader(old_shader_handle);
        REQUIRE(old_shader.is_valid());
        old_shader_uuid = old_shader.uuid();
        char derived_uuid[TC_UUID_SIZE];
        tc_shader_program_make_phase_uuid(
            derived_uuid, sizeof(derived_uuid), program.uuid(), "opaque");
        CHECK_EQ(old_shader_uuid, std::string(derived_uuid));

        const tc_shader_program_phase_desc replacement_phase = {
            "forward", 9, tc_render_state_transparent()};
        const tc_shader_program_payload_desc replacement = {
            "Program Test Reloaded", nullptr, "slang", 8, nullptr, 0, &replacement_phase, 1};
        REQUIRE(tc_shader_program_set_payload(program.get(), &replacement));
        CHECK_EQ(program.version(), 3u);
        CHECK_EQ(program.get()->phase_count, 1u);
        CHECK(old_shader.is_valid());

        const tc_shader_program_phase_desc duplicate_phases[] = {
            {"forward", 0, tc_render_state_opaque()},
            {"forward", 1, tc_render_state_opaque()},
        };
        const tc_shader_program_payload_desc rejected = {
            "Rejected", nullptr, nullptr, 0, nullptr, 0, duplicate_phases, 2};
        CHECK(!tc_shader_program_set_payload(program.get(), &rejected));
        CHECK_EQ(program.version(), 3u);
        CHECK_EQ(std::string(program.name()), "Program Test Reloaded");
        CHECK(!tc_shader_program_remove(program.handle));
    }

    CHECK(!tc_shader_program_is_valid(stale));
    CHECK(tc_shader_program_handle_is_invalid(tc_shader_program_find("program-test-uuid")));
    CHECK(tc_shader_handle_is_invalid(tc_shader_find(old_shader_uuid.c_str())));

    tc_shader_program_shutdown();
    tc_shader_shutdown();
}

TEST_CASE("shader program declare is canonical by UUID") {
    tc_shader_program_init();
    {
        TcShaderProgram first = TcShaderProgram::declare("canonical-program", "First");
        TcShaderProgram second = TcShaderProgram::declare("canonical-program", "Ignored");
        REQUIRE(first.is_valid());
        REQUIRE(second.is_valid());
        CHECK(tc_shader_program_handle_eq(first.handle, second.handle));
        CHECK_EQ(std::string(second.name()), "First");
        CHECK_EQ(tc_shader_program_count(), 1u);
    }
    CHECK_EQ(tc_shader_program_count(), 0u);
    tc_shader_program_shutdown();
}
