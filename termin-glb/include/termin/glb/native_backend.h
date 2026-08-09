#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(TERMIN_GLB_EXPORTS)
#define TERMIN_GLB_API __declspec(dllexport)
#else
#define TERMIN_GLB_API __declspec(dllimport)
#endif
#else
#define TERMIN_GLB_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TERMIN_GLB_CGLTF_VERSION "1.15"
#define TERMIN_GLB_CGLTF_REVISION "85cd62382dfea638278962690cf515023f33ed00"

typedef enum termin_glb_error_code {
    TERMIN_GLB_ERROR_NONE = 0,
    TERMIN_GLB_ERROR_IO,
    TERMIN_GLB_ERROR_INVALID_FORMAT,
    TERMIN_GLB_ERROR_UNSUPPORTED,
    TERMIN_GLB_ERROR_OUT_OF_MEMORY,
    TERMIN_GLB_ERROR_INTERNAL,
} termin_glb_error_code;

#define TERMIN_GLB_ERROR_MESSAGE_SIZE 512

typedef struct termin_glb_error {
    termin_glb_error_code code;
    char message[TERMIN_GLB_ERROR_MESSAGE_SIZE];
} termin_glb_error;

typedef struct termin_glb_document termin_glb_document;

typedef struct termin_glb_mesh_info {
    const char* name;
    size_t primitive_count;
    size_t vertex_count;
    size_t index_count;
    bool skinned;
} termin_glb_mesh_info;

typedef struct termin_glb_primitive_info {
    size_t first_index;
    size_t index_count;
    bool has_material;
    size_t material_index;
    uint32_t material_slot;
} termin_glb_primitive_info;

typedef struct termin_glb_image_info {
    const char* name;
    bool has_name;
    const char* mime_type;
    const char* uri;
    bool embedded;
    size_t encoded_size;
} termin_glb_image_info;

typedef struct termin_glb_texture_info {
    const char* name;
    bool has_name;
    bool has_image;
    size_t image_index;
    bool has_sampler;
    size_t sampler_index;
    bool selected_webp;
    bool selected_basisu;
    int mag_filter;
    int min_filter;
    int wrap_s;
    int wrap_t;
} termin_glb_texture_info;

typedef struct termin_glb_texture_view_info {
    bool present;
    size_t texture_index;
    int texcoord;
    float scale;
    bool has_transform;
} termin_glb_texture_view_info;

typedef struct termin_glb_material_info {
    const char* name;
    float base_color_factor[4];
    float metallic_factor;
    float roughness_factor;
    termin_glb_texture_view_info base_color_texture;
    termin_glb_texture_view_info metallic_roughness_texture;
    termin_glb_texture_view_info normal_texture;
    termin_glb_texture_view_info occlusion_texture;
    termin_glb_texture_view_info emissive_texture;
    float emissive_factor[3];
    int alpha_mode;
    float alpha_cutoff;
    bool double_sided;
    bool unlit;
    float ior;
    float specular_factor;
    float specular_color_factor[3];
} termin_glb_material_info;

typedef struct termin_glb_node_info {
    const char* name;
    bool has_parent;
    size_t parent_index;
    bool has_mesh;
    size_t mesh_index;
    bool has_skin;
    size_t skin_index;
    bool default_scene_root;
    bool has_matrix;
    float translation[3];
    float rotation[4];
    float scale[3];
    float matrix[16];
} termin_glb_node_info;

typedef struct termin_glb_skin_info {
    const char* name;
    size_t joint_count;
    bool has_skeleton;
    size_t skeleton_node_index;
    bool has_inverse_bind_matrices;
} termin_glb_skin_info;

typedef struct termin_glb_animation_info {
    const char* name;
    size_t sampler_count;
    size_t channel_count;
} termin_glb_animation_info;

typedef struct termin_glb_animation_sampler_info {
    size_t input_count;
    size_t output_float_count;
    size_t output_components;
    int interpolation;
} termin_glb_animation_sampler_info;

typedef struct termin_glb_animation_channel_info {
    size_t sampler_index;
    bool has_target_node;
    size_t target_node_index;
    int target_path;
} termin_glb_animation_channel_info;

TERMIN_GLB_API const char* termin_glb_backend_name(void);
TERMIN_GLB_API const char* termin_glb_cgltf_version(void);
TERMIN_GLB_API const char* termin_glb_cgltf_revision(void);
TERMIN_GLB_API const char* termin_glb_error_code_name(termin_glb_error_code code);
TERMIN_GLB_API void termin_glb_error_clear(termin_glb_error* error);

TERMIN_GLB_API termin_glb_document* termin_glb_document_open(const char* path, termin_glb_error* error);
TERMIN_GLB_API void termin_glb_document_close(termin_glb_document* document);
TERMIN_GLB_API size_t termin_glb_document_mesh_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_mesh_info(const termin_glb_document* document,
                                                  size_t mesh_index,
                                                  termin_glb_mesh_info* info,
                                                  termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_primitive_info(const termin_glb_document* document,
                                                       size_t mesh_index,
                                                       size_t primitive_index,
                                                       termin_glb_primitive_info* info,
                                                       termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_image_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_image_info(const termin_glb_document* document,
                                                   size_t image_index,
                                                   termin_glb_image_info* info,
                                                   termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_image_payload(const termin_glb_document* document,
                                                      size_t image_index,
                                                      const unsigned char** data,
                                                      size_t* size,
                                                      termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_texture_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_texture_info(const termin_glb_document* document,
                                                     size_t texture_index,
                                                     termin_glb_texture_info* info,
                                                     termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_material_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_material_info(const termin_glb_document* document,
                                                      size_t material_index,
                                                      termin_glb_material_info* info,
                                                      termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_node_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_node_info(const termin_glb_document* document,
                                                  size_t node_index,
                                                  termin_glb_node_info* info,
                                                  termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_skin_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_skin_info(const termin_glb_document* document,
                                                  size_t skin_index,
                                                  termin_glb_skin_info* info,
                                                  termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_skin_joints(const termin_glb_document* document,
                                                    size_t skin_index,
                                                    size_t* node_indices,
                                                    size_t node_index_count,
                                                    termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_skin_inverse_bind_matrices(const termin_glb_document* document,
                                                                   size_t skin_index,
                                                                   float* matrices,
                                                                   size_t float_count,
                                                                   termin_glb_error* error);
TERMIN_GLB_API size_t termin_glb_document_animation_count(const termin_glb_document* document);
TERMIN_GLB_API bool termin_glb_document_animation_info(const termin_glb_document* document,
                                                       size_t animation_index,
                                                       termin_glb_animation_info* info,
                                                       termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_animation_sampler_info(
    const termin_glb_document* document,
    size_t animation_index,
    size_t sampler_index,
    termin_glb_animation_sampler_info* info,
    termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_animation_sampler_payload(const termin_glb_document* document,
                                                                  size_t animation_index,
                                                                  size_t sampler_index,
                                                                  float* input,
                                                                  size_t input_count,
                                                                  float* output,
                                                                  size_t output_float_count,
                                                                  termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_animation_channel_info(
    const termin_glb_document* document,
    size_t animation_index,
    size_t channel_index,
    termin_glb_animation_channel_info* info,
    termin_glb_error* error);
TERMIN_GLB_API bool termin_glb_document_build_mesh(termin_glb_document* document,
                                                   size_t mesh_index,
                                                   const char* mesh_uuid,
                                                   const char* mesh_name,
                                                   bool convert_to_z_up,
                                                   termin_glb_error* error);
/* Compatibility name from the initial static-only migration stage. */
TERMIN_GLB_API bool termin_glb_document_build_static_mesh(termin_glb_document* document,
                                                          size_t mesh_index,
                                                          const char* mesh_uuid,
                                                          const char* mesh_name,
                                                          bool convert_to_z_up,
                                                          termin_glb_error* error);

#ifdef __cplusplus
}
#endif
