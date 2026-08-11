#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <termin/nodegraph/c_api.h>

static bool reject_connection(void* userdata, const tc_nodegraph_connection_proposal* proposal) {
    int* calls = (int*)userdata;
    ++*calls;
    assert(strcmp(proposal->source_type, "float") == 0);
    return false;
}

static tc_nodegraph_node_desc node_descriptor(const char* id,
                                              const tc_nodegraph_socket_desc* inputs,
                                              size_t input_count,
                                              const tc_nodegraph_socket_desc* outputs,
                                              size_t output_count,
                                              const tc_value* params) {
    tc_nodegraph_node_desc result = {0};
    result.struct_size = sizeof(result);
    result.id = id;
    result.kind = "test";
    result.title = id;
    result.width = 190.0f;
    result.height = 120.0f;
    result.inputs = inputs;
    result.input_count = input_count;
    result.outputs = outputs;
    result.output_count = output_count;
    result.params = params;
    return result;
}

int main(void) {
    tc_nodegraph_handle graph = tc_nodegraph_create();
    assert(!tc_nodegraph_handle_is_invalid(graph));
    assert(tc_nodegraph_is_valid(graph));

    tc_value params = tc_value_dict_new();
    tc_value_dict_set(&params, "quality", tc_value_int(3));
    tc_value nested = tc_value_list_new();
    tc_value_list_push(&nested, tc_value_string("one"));
    tc_value_list_push(&nested, tc_value_bool(true));
    tc_value_dict_set(&params, "nested", nested);

    tc_nodegraph_socket_desc output = {sizeof(tc_nodegraph_socket_desc), "value", "float", true};
    tc_nodegraph_socket_desc input = {sizeof(tc_nodegraph_socket_desc), "value", "float", false};
    tc_nodegraph_node_desc source_desc = node_descriptor("source", NULL, 0, &output, 1, &params);
    tc_nodegraph_node_desc sink_desc = node_descriptor("sink", &input, 1, NULL, 0, NULL);
    tc_nodegraph_node_handle source = tc_nodegraph_entity_handle_invalid();
    tc_nodegraph_node_handle sink = tc_nodegraph_entity_handle_invalid();
    assert(tc_nodegraph_create_node(graph, &source_desc, &source) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_create_node(graph, &sink_desc, &sink) == TC_NODEGRAPH_OK);
    tc_value_free(&params);
    source_desc.params = NULL;

    tc_nodegraph_edge_handle edge = tc_nodegraph_entity_handle_invalid();
    assert(tc_nodegraph_connect(graph, source, "value", sink, "value", "edge", &edge) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_node_count(graph) == 2);
    assert(tc_nodegraph_edge_count(graph) == 1);
    assert(tc_nodegraph_copy_nodes(graph, NULL, 0) == 2);
    tc_nodegraph_node_handle nodes[2];
    assert(tc_nodegraph_copy_nodes(graph, nodes, 2) == 2);

    tc_value source_snapshot = tc_value_nil();
    assert(tc_nodegraph_copy_node_value(graph, source, &source_snapshot) == TC_NODEGRAPH_OK);
    tc_value* copied_params = tc_value_dict_get(&source_snapshot, "params");
    assert(copied_params != NULL);
    assert(tc_value_dict_get(copied_params, "quality")->data.i == 3);
    tc_value_free(&source_snapshot);

    tc_value graph_value = tc_value_nil();
    assert(tc_nodegraph_serialize(graph, &graph_value) == TC_NODEGRAPH_OK);
    tc_nodegraph_handle clone = tc_nodegraph_create();
    assert(tc_nodegraph_replace(clone, &graph_value) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_node_count(clone) == 2);
    assert(tc_nodegraph_edge_count(clone) == 1);

    size_t json_size = tc_nodegraph_copy_json(graph, 2, NULL, 0);
    assert(json_size > 1);
    char* json = (char*)malloc(json_size);
    assert(json != NULL);
    assert(tc_nodegraph_copy_json(graph, 2, json, json_size) == json_size);
    tc_nodegraph_handle json_clone = tc_nodegraph_create();
    assert(tc_nodegraph_replace_json(json_clone, json) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_node_count(json_clone) == 2);
    free(json);

    const uint64_t clone_revision = tc_nodegraph_revision(clone);
    assert(tc_nodegraph_replace_json(clone, "{\"nodes\":[],\"edges\":[]}") == TC_NODEGRAPH_INVALID_VALUE);
    assert(tc_nodegraph_revision(clone) == clone_revision);
    assert(tc_nodegraph_node_count(clone) == 2);
    size_t error_size = tc_nodegraph_copy_last_error(clone, NULL, 0);
    assert(error_size > 1);
    char* error = (char*)malloc(error_size);
    assert(error != NULL);
    tc_nodegraph_copy_last_error(clone, error, error_size);
    assert(strstr(error, "groups") != NULL);
    free(error);

    assert(tc_nodegraph_move_node(clone, source, 1.0f, 2.0f) == TC_NODEGRAPH_INVALID_HANDLE);
    assert(tc_nodegraph_remove_node(graph, source) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_move_node(graph, source, 1.0f, 2.0f) == TC_NODEGRAPH_INVALID_HANDLE);

    int validator_calls = 0;
    tc_nodegraph_handle rejected =
        tc_nodegraph_create_with_validator(reject_connection, &validator_calls, NULL);
    tc_nodegraph_node_handle rejected_source = tc_nodegraph_entity_handle_invalid();
    tc_nodegraph_node_handle rejected_sink = tc_nodegraph_entity_handle_invalid();
    assert(tc_nodegraph_create_node(rejected, &source_desc, &rejected_source) == TC_NODEGRAPH_OK);
    assert(tc_nodegraph_create_node(rejected, &sink_desc, &rejected_sink) == TC_NODEGRAPH_OK);
    tc_nodegraph_edge_handle rejected_edge = tc_nodegraph_entity_handle_invalid();
    assert(tc_nodegraph_connect(rejected,
                                rejected_source,
                                "value",
                                rejected_sink,
                                "value",
                                NULL,
                                &rejected_edge) == TC_NODEGRAPH_TYPE_MISMATCH);
    assert(validator_calls == 1);
    assert(tc_nodegraph_edge_count(rejected) == 0);

    tc_value_free(&graph_value);
    tc_nodegraph_destroy(rejected);
    tc_nodegraph_destroy(json_clone);
    tc_nodegraph_destroy(clone);
    tc_nodegraph_destroy(graph);
    assert(!tc_nodegraph_is_valid(graph));
    return 0;
}
