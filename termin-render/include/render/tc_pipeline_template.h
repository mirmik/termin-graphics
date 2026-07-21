#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_resource.h>
#include <tcbase/tc_binding_types.h>
#include <tcbase/types/api.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_DEFINE_HANDLE(tc_pipeline_template_handle)

#define TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION 2u
#define TC_PIPELINE_TEMPLATE_BINARY_VERSION 2u

typedef enum tc_pipeline_resource_access {
    TC_PIPELINE_RESOURCE_READ = 1,
    TC_PIPELINE_RESOURCE_WRITE = 2,
    TC_PIPELINE_RESOURCE_READ_WRITE = 3
} tc_pipeline_resource_access;

typedef struct tc_pipeline_template_pass_desc {
    const char* type_name;
    const char* name;
    const char* parameters;
    const char* viewport_name;
} tc_pipeline_template_pass_desc;

typedef struct tc_pipeline_template_resource_desc {
    const char* name;
    const char* resource_type;
    const char* format;
    const char* viewport_name;
    int32_t width;
    int32_t height;
    float scale;
    uint32_t samples;
    uint32_t flags;
} tc_pipeline_template_resource_desc;

typedef struct tc_pipeline_template_dependency_desc {
    uint32_t pass_index;
    const char* resource;
    tc_pipeline_resource_access access;
} tc_pipeline_template_dependency_desc;

typedef struct tc_pipeline_template_target_desc {
    const char* viewport_name;
    const char* export_name;
    int32_t width;
    int32_t height;
} tc_pipeline_template_target_desc;

typedef enum tc_pipeline_attachment_kind {
    TC_PIPELINE_ATTACHMENT_COLOR = 1,
    TC_PIPELINE_ATTACHMENT_DEPTH = 2
} tc_pipeline_attachment_kind;

typedef struct tc_pipeline_template_resource_view_desc {
    const char* name;
    const char* parent;
    tc_pipeline_attachment_kind attachment;
} tc_pipeline_template_resource_view_desc;

typedef struct tc_pipeline_template_fbo_composition_desc {
    const char* name;
    const char* color;
    const char* depth;
} tc_pipeline_template_fbo_composition_desc;

typedef struct tc_pipeline_template_payload_desc {
    uint32_t descriptor_version;
    const char* name;
    const tc_pipeline_template_pass_desc* passes;
    uint32_t pass_count;
    const tc_pipeline_template_resource_desc* resources;
    uint32_t resource_count;
    const tc_pipeline_template_dependency_desc* dependencies;
    uint32_t dependency_count;
    const tc_pipeline_template_target_desc* targets;
    uint32_t target_count;
    const tc_pipeline_template_resource_view_desc* resource_views;
    uint32_t resource_view_count;
    const tc_pipeline_template_fbo_composition_desc* fbo_compositions;
    uint32_t fbo_composition_count;
} tc_pipeline_template_payload_desc;

typedef struct tc_pipeline_template {
    tc_resource_header header;
    tc_pipeline_template_handle self_handle;
    uint32_t descriptor_version;
    tc_pipeline_template_pass_desc* passes;
    uint32_t pass_count;
    tc_pipeline_template_resource_desc* resources;
    uint32_t resource_count;
    tc_pipeline_template_dependency_desc* dependencies;
    uint32_t dependency_count;
    tc_pipeline_template_target_desc* targets;
    uint32_t target_count;
    tc_pipeline_template_resource_view_desc* resource_views;
    uint32_t resource_view_count;
    tc_pipeline_template_fbo_composition_desc* fbo_compositions;
    uint32_t fbo_composition_count;
} tc_pipeline_template;

#ifdef __cplusplus
}
#endif
