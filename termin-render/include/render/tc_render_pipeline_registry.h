#pragma once

#include <render/tc_render_pipeline.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_API void tc_render_pipeline_init(void);
TC_API void tc_render_pipeline_shutdown(void);
TC_API tc_render_pipeline_handle tc_render_pipeline_create(const char* uuid, const char* name);
TC_API tc_render_pipeline_handle tc_render_pipeline_declare(const char* uuid, const char* name);
TC_API tc_render_pipeline_handle tc_render_pipeline_find(const char* uuid);
TC_API tc_render_pipeline* tc_render_pipeline_get(tc_render_pipeline_handle handle);
TC_API bool tc_render_pipeline_is_valid(tc_render_pipeline_handle handle);
TC_API size_t tc_render_pipeline_count(void);
TC_API void tc_render_pipeline_retain(tc_render_pipeline* pipeline);
TC_API bool tc_render_pipeline_release(tc_render_pipeline* pipeline);
TC_API bool tc_render_pipeline_remove(tc_render_pipeline_handle handle);
TC_API bool tc_render_pipeline_set_payload(
    tc_render_pipeline* pipeline,
    const tc_render_pipeline_payload_desc* desc);
TC_API uint32_t tc_render_pipeline_version(const tc_render_pipeline* pipeline);

/* Returns the required byte count. Writes only when capacity is sufficient. */
TC_API size_t tc_render_pipeline_serialize(
    const tc_render_pipeline* pipeline,
    uint8_t* output,
    size_t capacity);
TC_API tc_render_pipeline_handle tc_render_pipeline_deserialize(
    const char* uuid,
    const uint8_t* data,
    size_t size);

#ifdef __cplusplus
}
#endif
