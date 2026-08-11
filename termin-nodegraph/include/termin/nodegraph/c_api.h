#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <tcbase/tc_binding_types.h>
#include <tcbase/tc_value.h>

#include <termin/nodegraph/export.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_DEFINE_HANDLE(tc_nodegraph_handle)

typedef struct tc_nodegraph_entity_handle {
    uint64_t graph_id;
    uint32_t index;
    uint32_t generation;
} tc_nodegraph_entity_handle;

typedef tc_nodegraph_entity_handle tc_nodegraph_node_handle;
typedef tc_nodegraph_entity_handle tc_nodegraph_edge_handle;
typedef tc_nodegraph_entity_handle tc_nodegraph_group_handle;

typedef enum tc_nodegraph_result {
    TC_NODEGRAPH_OK = 0,
    TC_NODEGRAPH_INVALID_ARGUMENT,
    TC_NODEGRAPH_INVALID_HANDLE,
    TC_NODEGRAPH_DUPLICATE_ID,
    TC_NODEGRAPH_INVALID_ID,
    TC_NODEGRAPH_NODE_NOT_FOUND,
    TC_NODEGRAPH_EDGE_NOT_FOUND,
    TC_NODEGRAPH_GROUP_NOT_FOUND,
    TC_NODEGRAPH_SOCKET_NOT_FOUND,
    TC_NODEGRAPH_DUPLICATE_SOCKET,
    TC_NODEGRAPH_SELF_LINK,
    TC_NODEGRAPH_TYPE_MISMATCH,
    TC_NODEGRAPH_CARDINALITY_VIOLATION,
    TC_NODEGRAPH_INVALID_VALUE,
    TC_NODEGRAPH_INTERNAL_ERROR,
} tc_nodegraph_result;

typedef struct tc_nodegraph_socket_desc {
    size_t struct_size;
    const char* name;
    const char* socket_type;
    bool multi;
} tc_nodegraph_socket_desc;

typedef struct tc_nodegraph_node_desc {
    size_t struct_size;
    const char* id;
    const char* kind;
    const char* title;
    float x;
    float y;
    float width;
    float height;
    const tc_nodegraph_socket_desc* inputs;
    size_t input_count;
    const tc_nodegraph_socket_desc* outputs;
    size_t output_count;
    const tc_value* params;
    const tc_value* data;
} tc_nodegraph_node_desc;

typedef struct tc_nodegraph_group_desc {
    size_t struct_size;
    const char* id;
    const char* title;
    float x;
    float y;
    float width;
    float height;
    const tc_value* data;
} tc_nodegraph_group_desc;

typedef struct tc_nodegraph_connection_proposal {
    size_t struct_size;
    const char* source_node_id;
    const char* source_socket;
    const char* source_type;
    const char* destination_node_id;
    const char* destination_socket;
    const char* destination_type;
} tc_nodegraph_connection_proposal;

// Proposal strings are borrowed for the duration of the callback. Validators
// must not recursively mutate the graph. destroy_userdata runs on graph destroy.
typedef bool (*tc_nodegraph_connection_validator)(void* userdata,
                                                  const tc_nodegraph_connection_proposal* proposal);
typedef void (*tc_nodegraph_userdata_deleter)(void* userdata);

static inline tc_nodegraph_entity_handle tc_nodegraph_entity_handle_invalid(void) {
    tc_nodegraph_entity_handle handle = {0, UINT32_MAX, 0};
    return handle;
}

static inline bool tc_nodegraph_entity_handle_is_invalid(tc_nodegraph_entity_handle handle) {
    return handle.graph_id == 0 || handle.index == UINT32_MAX || handle.generation == 0;
}

TERMIN_NODEGRAPH_CORE_API tc_nodegraph_handle tc_nodegraph_create(void);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_handle
tc_nodegraph_create_with_validator(tc_nodegraph_connection_validator validator,
                                   void* userdata,
                                   tc_nodegraph_userdata_deleter destroy_userdata);
TERMIN_NODEGRAPH_CORE_API void tc_nodegraph_destroy(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API bool tc_nodegraph_is_valid(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API uint64_t tc_nodegraph_id(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API uint64_t tc_nodegraph_revision(tc_nodegraph_handle graph);

// The returned size includes the trailing NUL. Passing NULL/0 is a size query.
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_copy_last_error(tc_nodegraph_handle graph,
                                                              char* buffer,
                                                              size_t capacity);

TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_node_count(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_edge_count(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_group_count(tc_nodegraph_handle graph);
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_copy_nodes(tc_nodegraph_handle graph,
                                                         tc_nodegraph_node_handle* out_handles,
                                                         size_t capacity);
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_copy_edges(tc_nodegraph_handle graph,
                                                         tc_nodegraph_edge_handle* out_handles,
                                                         size_t capacity);
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_copy_groups(tc_nodegraph_handle graph,
                                                          tc_nodegraph_group_handle* out_handles,
                                                          size_t capacity);

TERMIN_NODEGRAPH_CORE_API bool tc_nodegraph_find_node(tc_nodegraph_handle graph,
                                                      const char* id,
                                                      tc_nodegraph_node_handle* out_node);
TERMIN_NODEGRAPH_CORE_API bool tc_nodegraph_find_edge(tc_nodegraph_handle graph,
                                                      const char* id,
                                                      tc_nodegraph_edge_handle* out_edge);
TERMIN_NODEGRAPH_CORE_API bool tc_nodegraph_find_group(tc_nodegraph_handle graph,
                                                       const char* id,
                                                       tc_nodegraph_group_handle* out_group);

TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_create_node(tc_nodegraph_handle graph,
                                                                       const tc_nodegraph_node_desc* descriptor,
                                                                       tc_nodegraph_node_handle* out_node);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_remove_node(tc_nodegraph_handle graph,
                                                                       tc_nodegraph_node_handle node);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_move_node(tc_nodegraph_handle graph,
                                                                     tc_nodegraph_node_handle node,
                                                                     float x,
                                                                     float y);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_add_input(tc_nodegraph_handle graph,
                                                                     tc_nodegraph_node_handle node,
                                                                     const tc_nodegraph_socket_desc* socket);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_add_output(tc_nodegraph_handle graph,
                                                                      tc_nodegraph_node_handle node,
                                                                      const tc_nodegraph_socket_desc* socket);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_set_node_param(tc_nodegraph_handle graph,
                                                                          tc_nodegraph_node_handle node,
                                                                          const char* name,
                                                                          const tc_value* value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_set_node_data(tc_nodegraph_handle graph,
                                                                         tc_nodegraph_node_handle node,
                                                                         const tc_value* value);

TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_connect(tc_nodegraph_handle graph,
                                                                   tc_nodegraph_node_handle source_node,
                                                                   const char* source_socket,
                                                                   tc_nodegraph_node_handle destination_node,
                                                                   const char* destination_socket,
                                                                   const char* edge_id,
                                                                   tc_nodegraph_edge_handle* out_edge);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_remove_edge(tc_nodegraph_handle graph,
                                                                       tc_nodegraph_edge_handle edge);

TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_create_group(tc_nodegraph_handle graph,
                                                                        const tc_nodegraph_group_desc* descriptor,
                                                                        tc_nodegraph_group_handle* out_group);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_remove_group(tc_nodegraph_handle graph,
                                                                        tc_nodegraph_group_handle group);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_move_group(tc_nodegraph_handle graph,
                                                                      tc_nodegraph_group_handle group,
                                                                      float x,
                                                                      float y);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_set_group_data(tc_nodegraph_handle graph,
                                                                          tc_nodegraph_group_handle group,
                                                                          const tc_value* value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_set_data(tc_nodegraph_handle graph,
                                                                    const tc_value* value);

// Snapshot functions return a deep-owned tc_value. The caller must call tc_value_free.
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_copy_node_value(tc_nodegraph_handle graph,
                                                                           tc_nodegraph_node_handle node,
                                                                           tc_value* out_value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_copy_edge_value(tc_nodegraph_handle graph,
                                                                           tc_nodegraph_edge_handle edge,
                                                                           tc_value* out_value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_copy_group_value(tc_nodegraph_handle graph,
                                                                            tc_nodegraph_group_handle group,
                                                                            tc_value* out_value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_serialize(tc_nodegraph_handle graph,
                                                                     tc_value* out_value);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_replace(tc_nodegraph_handle graph,
                                                                   const tc_value* value);

// The returned size includes the trailing NUL. Passing NULL/0 is a size query.
TERMIN_NODEGRAPH_CORE_API size_t tc_nodegraph_copy_json(tc_nodegraph_handle graph,
                                                        int indent,
                                                        char* buffer,
                                                        size_t capacity);
TERMIN_NODEGRAPH_CORE_API tc_nodegraph_result tc_nodegraph_replace_json(tc_nodegraph_handle graph,
                                                                        const char* json);

#ifdef __cplusplus
}
#endif
