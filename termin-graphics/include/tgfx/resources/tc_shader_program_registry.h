#pragma once

#include <tgfx/resources/tc_shader_program.h>

#ifdef __cplusplus
extern "C" {
#endif

TGFX_API void tc_shader_program_init(void);
TGFX_API void tc_shader_program_shutdown(void);

TGFX_API tc_shader_program_handle tc_shader_program_create(const char* uuid, const char* name);
TGFX_API tc_shader_program_handle tc_shader_program_declare(const char* uuid, const char* name);
TGFX_API tc_shader_program_handle tc_shader_program_get_or_create(const char* uuid, const char* name);
TGFX_API tc_shader_program_handle tc_shader_program_find(const char* uuid);
TGFX_API tc_shader_program* tc_shader_program_get(tc_shader_program_handle handle);
TGFX_API bool tc_shader_program_is_valid(tc_shader_program_handle handle);
TGFX_API bool tc_shader_program_contains(const char* uuid);
TGFX_API size_t tc_shader_program_count(void);

typedef struct tc_shader_program_info {
    tc_shader_program_handle handle;
    char uuid[TC_UUID_SIZE];
    const char* name;
    const char* source_path;
    const char* language;
    uint32_t ref_count;
    uint32_t version;
    uint32_t property_count;
    uint32_t phase_count;
    uint8_t is_loaded;
    uint8_t _pad[7];
} tc_shader_program_info;

TGFX_API tc_shader_program_info* tc_shader_program_get_all_info(size_t* count);

TGFX_API void tc_shader_program_retain(tc_shader_program* program);
TGFX_API bool tc_shader_program_release(tc_shader_program* program);
TGFX_API bool tc_shader_program_remove(tc_shader_program_handle handle);

TGFX_API bool tc_shader_program_set_payload(
    tc_shader_program* program,
    const tc_shader_program_payload_desc* desc
);
TGFX_API uint32_t tc_shader_program_version(const tc_shader_program* program);
TGFX_API const tc_shader_program_phase* tc_shader_program_find_phase(
    const tc_shader_program* program,
    const char* phase_mark
);
TGFX_API void tc_shader_program_make_phase_uuid(
    char* out_uuid,
    size_t out_size,
    const char* program_uuid,
    const char* phase_mark
);

#ifdef __cplusplus
}
#endif
