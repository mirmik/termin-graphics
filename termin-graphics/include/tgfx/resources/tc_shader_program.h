#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_resource.h>
#include <tgfx/resources/tc_material.h>
#include <tgfx/resources/tc_shader.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_DEFINE_HANDLE(tc_shader_program_handle)

#define TC_SHADER_PROGRAM_PROPERTY_TYPE_MAX 16
#define TC_SHADER_PROGRAM_PROPERTY_LABEL_MAX 64
#define TC_SHADER_PROGRAM_PROPERTY_DEFAULT_TEXT_MAX 64

typedef struct tc_shader_program_property {
    char name[TC_UNIFORM_NAME_MAX];
    char property_type[TC_SHADER_PROGRAM_PROPERTY_TYPE_MAX];
    char label[TC_SHADER_PROGRAM_PROPERTY_LABEL_MAX];
    char default_text[TC_SHADER_PROGRAM_PROPERTY_DEFAULT_TEXT_MAX];
    tc_uniform_value default_value;
    double range_min;
    double range_max;
    uint8_t has_default;
    uint8_t has_range_min;
    uint8_t has_range_max;
    uint8_t _reserved;
} tc_shader_program_property;

typedef struct tc_shader_program_phase {
    char phase_mark[TC_PHASE_MARK_MAX];
    int32_t priority;
    tc_render_state state;
    tc_shader_handle shader;
} tc_shader_program_phase;

typedef struct tc_shader_program_property_desc {
    const char* name;
    const char* property_type;
    const char* label;
    const tc_uniform_value* default_value;
    const char* default_text;
    double range_min;
    double range_max;
    uint8_t has_range_min;
    uint8_t has_range_max;
} tc_shader_program_property_desc;

typedef struct tc_shader_program_phase_desc {
    const char* phase_mark;
    int32_t priority;
    tc_render_state state;
} tc_shader_program_phase_desc;

typedef struct tc_shader_program_payload_desc {
    const char* name;
    const char* source_path;
    const char* language;
    uint32_t features;
    const tc_shader_program_property_desc* properties;
    uint32_t property_count;
    const tc_shader_program_phase_desc* phases;
    uint32_t phase_count;
} tc_shader_program_payload_desc;

typedef struct tc_shader_program {
    tc_resource_header header;
    tc_shader_program_handle self_handle;
    const char* source_path;
    const char* language;
    uint32_t features;
    tc_shader_program_property* properties;
    uint32_t property_count;
    tc_shader_program_phase* phases;
    uint32_t phase_count;
} tc_shader_program;

#ifdef __cplusplus
}
#endif
