// tc_gpu.c - GPU operations for termin (material-specific only)
// Resource GPU ops (texture, shader, mesh) have been moved to termin-graphics
// (tgfx_resource_gpu.c).
#include <tgfx/tc_gpu.h>
#include <tcbase/tc_log.h>
#include <tgfx/resources/tc_shader_registry.h>
#include <tgfx/resources/tc_texture_registry.h>

// ============================================================================
// Material GPU operations
// ============================================================================

void tc_material_phase_apply_textures(tc_material_phase* phase) {
    if (!phase) return;

    for (size_t i = 0; i < phase->texture_count; i++) {
        tc_texture* tex = tc_texture_get(phase->textures[i].texture);
        if (tex) {
            tc_texture_bind_gpu(tex, (int)i);
        } else {
            tc_log(TC_LOG_WARN, "tc_material_phase_apply_textures: texture '%s' is invalid (handle %d:%d)",
                   phase->textures[i].name,
                   phase->textures[i].texture.index,
                   phase->textures[i].texture.generation);
        }
    }
}

void tc_material_phase_apply_uniforms(tc_material_phase* phase, tc_shader* shader) {
    if (!phase || !shader) return;

    for (size_t i = 0; i < phase->uniform_count; i++) {
        const tc_uniform_value* u = &phase->uniforms[i];
        switch (u->type) {
            case TC_UNIFORM_BOOL:
            case TC_UNIFORM_INT:
                tc_shader_set_int(shader, u->name, u->data.i);
                break;
            case TC_UNIFORM_FLOAT:
                tc_shader_set_float(shader, u->name, u->data.f);
                break;
            case TC_UNIFORM_VEC2:
                tc_shader_set_vec2(shader, u->name, u->data.v2[0], u->data.v2[1]);
                break;
            case TC_UNIFORM_VEC3:
                tc_shader_set_vec3(shader, u->name, u->data.v3[0], u->data.v3[1], u->data.v3[2]);
                break;
            case TC_UNIFORM_VEC4:
                tc_shader_set_vec4(shader, u->name, u->data.v4[0], u->data.v4[1], u->data.v4[2], u->data.v4[3]);
                break;
            case TC_UNIFORM_MAT4:
                tc_shader_set_mat4(shader, u->name, u->data.m4, true);
                break;
            case TC_UNIFORM_FLOAT_ARRAY:
                break;
            default:
                break;
        }
    }

    // Bind texture samplers
    for (size_t i = 0; i < phase->texture_count; i++) {
        tc_shader_set_int(shader, phase->textures[i].name, (int)i);
    }
}

bool tc_material_phase_apply_gpu(tc_material_phase* phase) {
    if (!phase) return false;

    tc_shader* shader = tc_shader_get(phase->shader);
    if (!shader) {
        tc_log(TC_LOG_ERROR, "tc_material_phase_apply_gpu: invalid shader handle");
        return false;
    }

    if (tc_shader_compile_gpu(shader) == 0) {
        tc_log(TC_LOG_ERROR, "tc_material_phase_apply_gpu: shader compile failed");
        return false;
    }
    tc_shader_use_gpu(shader);

    tc_material_phase_apply_textures(phase);
    tc_material_phase_apply_uniforms(phase, shader);

    return true;
}

void tc_material_phase_apply_with_mvp(
    tc_material_phase* phase,
    tc_shader* shader,
    const float* model,
    const float* view,
    const float* projection
) {
    if (!phase || !shader) return;

    // Always ensure shader is compiled and bound for current context
    tc_shader_use_gpu(shader);

    tc_shader_set_mat4(shader, "u_model", model, false);
    tc_shader_set_mat4(shader, "u_view", view, false);
    tc_shader_set_mat4(shader, "u_projection", projection, false);

    tc_material_phase_apply_textures(phase);
    tc_material_phase_apply_uniforms(phase, shader);
}
