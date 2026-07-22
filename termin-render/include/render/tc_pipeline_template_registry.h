#pragma once

#include <render/tc_pipeline_template.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_API void tc_pipeline_template_init(void);
TC_API void tc_pipeline_template_shutdown(void);
TC_API tc_pipeline_template_handle tc_pipeline_template_create(const char* uuid, const char* name);
TC_API tc_pipeline_template_handle tc_pipeline_template_declare(const char* uuid, const char* name);
TC_API tc_pipeline_template_handle tc_pipeline_template_find(const char* uuid);
TC_API tc_pipeline_template* tc_pipeline_template_get(tc_pipeline_template_handle handle);
TC_API bool tc_pipeline_template_is_valid(tc_pipeline_template_handle handle);
TC_API size_t tc_pipeline_template_count(void);

typedef struct tc_pipeline_template_info {
    tc_pipeline_template_handle handle;
    char uuid[TC_UUID_SIZE];
    const char* name;
    uint32_t ref_count;
    uint32_t version;
    uint32_t descriptor_version;
    uint32_t pass_count;
    uint32_t resource_count;
    uint32_t dependency_count;
    uint8_t is_loaded;
    uint8_t _pad[3];
} tc_pipeline_template_info;

TC_API tc_pipeline_template_info* tc_pipeline_template_get_all_info(size_t* count);
TC_API void tc_pipeline_template_retain(tc_pipeline_template* pipeline_template);
TC_API bool tc_pipeline_template_release(tc_pipeline_template* pipeline_template);
TC_API bool tc_pipeline_template_remove(tc_pipeline_template_handle handle);
TC_API bool tc_pipeline_template_set_payload(
    tc_pipeline_template* pipeline_template,
    const tc_pipeline_template_payload_desc* desc);
TC_API uint32_t tc_pipeline_template_version(const tc_pipeline_template* pipeline_template);

/* Returns the required byte count. Writes only when capacity is sufficient. */
TC_API size_t tc_pipeline_template_serialize(
    const tc_pipeline_template* pipeline_template,
    uint8_t* output,
    size_t capacity);
TC_API tc_pipeline_template_handle tc_pipeline_template_deserialize(
    const char* uuid,
    const uint8_t* data,
    size_t size);

#ifdef __cplusplus
}
#endif
