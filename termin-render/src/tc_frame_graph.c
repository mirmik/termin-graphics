#include <render/tc_frame_graph.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tcbase/tc_log.h>

#ifdef _WIN32
#define tc_strdup _strdup
#else
#define tc_strdup strdup
#endif

#define MAX_RESOURCES 256
#define MAX_PASSES 128
#define MAX_READS_PER_PASS 16
#define MAX_WRITES_PER_PASS 8

typedef struct {
    char* name;
    char* canonical;
    int writer_index;
} tc_fg_resource;

typedef struct {
    tc_pass* pass;
    int index;
    int in_degree;
    bool is_inplace;
    int* dependents;
    size_t dependent_count;
    size_t dependent_capacity;
} tc_fg_node;

struct tc_frame_graph {
    tc_fg_resource resources[MAX_RESOURCES];
    size_t resource_count;
    tc_fg_node nodes[MAX_PASSES];
    size_t node_count;
    tc_pass** schedule;
    size_t schedule_count;
    tc_frame_graph_error error;
    char error_message[512];
};

static tc_fg_resource* find_resource(tc_frame_graph* fg, const char* name) {
    for (size_t i = 0; i < fg->resource_count; i++) {
        if (strcmp(fg->resources[i].name, name) == 0) {
            return &fg->resources[i];
        }
    }
    return NULL;
}

static tc_fg_resource* get_or_create_resource(tc_frame_graph* fg, const char* name) {
    tc_fg_resource* res = find_resource(fg, name);
    if (res) return res;

    if (fg->resource_count >= MAX_RESOURCES) {
        tc_log(TC_LOG_ERROR, "[tc_frame_graph] Too many resources");
        return NULL;
    }

    res = &fg->resources[fg->resource_count++];
    res->name = tc_strdup(name);
    res->canonical = tc_strdup(name);
    res->writer_index = -1;
    return res;
}

static bool add_dependent(tc_fg_node* node, int dep_index) {
    for (size_t i = 0; i < node->dependent_count; i++) {
        if (node->dependents[i] == dep_index) return false;
    }

    if (node->dependent_count >= node->dependent_capacity) {
        size_t new_cap = node->dependent_capacity ? node->dependent_capacity * 2 : 8;
        node->dependents = (int*)realloc(node->dependents, new_cap * sizeof(int));
        node->dependent_capacity = new_cap;
    }
    node->dependents[node->dependent_count++] = dep_index;
    return true;
}

static bool build_dependency_graph(tc_frame_graph* fg, tc_pipeline_handle pipeline) {
    int pass_index = 0;

    size_t pipeline_pass_count = tc_pipeline_pass_count(pipeline);
    for (size_t pi = 0; pi < pipeline_pass_count; pi++) {
        tc_pass* pass = tc_pipeline_get_pass_at(pipeline, pi);
        if (!pass || !pass->enabled) {
            continue;
        }

        if (pass_index >= MAX_PASSES) {
            snprintf(fg->error_message, sizeof(fg->error_message), "Too many passes in pipeline");
            fg->error = TC_FG_ERROR_CYCLE;
            return false;
        }

        tc_fg_node* node = &fg->nodes[fg->node_count++];
        node->pass = pass;
        node->index = pass_index;
        node->in_degree = 0;
        node->is_inplace = tc_pass_is_inplace(pass);
        node->dependents = NULL;
        node->dependent_count = 0;
        node->dependent_capacity = 0;

        const char* writes[MAX_WRITES_PER_PASS];
        size_t write_count = tc_pass_get_writes(pass, writes, MAX_WRITES_PER_PASS);

        for (size_t i = 0; i < write_count; i++) {
            tc_fg_resource* res = get_or_create_resource(fg, writes[i]);
            if (!res) return false;

            if (res->writer_index >= 0) {
                snprintf(
                    fg->error_message,
                    sizeof(fg->error_message),
                    "Resource '%s' written by multiple passes: '%s' and '%s'",
                    writes[i],
                    fg->nodes[res->writer_index].pass->pass_name,
                    pass->pass_name
                );
                fg->error = TC_FG_ERROR_MULTI_WRITER;
                return false;
            }
            res->writer_index = pass_index;
        }

        const char* reads[MAX_READS_PER_PASS];
        size_t read_count = tc_pass_get_reads(pass, reads, MAX_READS_PER_PASS);
        for (size_t i = 0; i < read_count; i++) {
            get_or_create_resource(fg, reads[i]);
        }

        pass_index++;
    }

    bool changed = true;
    int max_iterations = 100;
    while (changed && max_iterations-- > 0) {
        changed = false;
        for (size_t i = 0; i < fg->node_count; i++) {
            tc_fg_node* node = &fg->nodes[i];
            const char* aliases[8];
            size_t alias_count = tc_pass_get_inplace_aliases(node->pass, aliases, 4);

            for (size_t j = 0; j < alias_count; j++) {
                const char* read_name = aliases[j * 2];
                const char* write_name = aliases[j * 2 + 1];

                tc_fg_resource* read_res = find_resource(fg, read_name);
                tc_fg_resource* write_res = find_resource(fg, write_name);

                if (read_res && write_res) {
                    if (strcmp(write_res->canonical, read_res->canonical) != 0) {
                        free(write_res->canonical);
                        write_res->canonical = tc_strdup(read_res->canonical);
                        changed = true;
                    }
                }
            }
        }
    }

    for (size_t i = 0; i < fg->node_count; i++) {
        tc_fg_node* node = &fg->nodes[i];
        const char* reads[MAX_READS_PER_PASS];
        size_t read_count = tc_pass_get_reads(node->pass, reads, MAX_READS_PER_PASS);

        for (size_t j = 0; j < read_count; j++) {
            tc_fg_resource* res = find_resource(fg, reads[j]);
            if (res && res->writer_index >= 0 && res->writer_index != (int)i) {
                tc_fg_node* writer_node = &fg->nodes[res->writer_index];
                if (add_dependent(writer_node, (int)i)) {
                    node->in_degree++;
                }
            }
        }
    }

    for (size_t i = 0; i < fg->node_count; i++) {
        tc_fg_node* node = &fg->nodes[i];
        if (!node->is_inplace) continue;

        const char* aliases[8];
        size_t alias_count = tc_pass_get_inplace_aliases(node->pass, aliases, 4);

        for (size_t j = 0; j < alias_count; j++) {
            const char* read_name = aliases[j * 2];

            for (size_t k = 0; k < fg->node_count; k++) {
                if (k == i) continue;

                tc_fg_node* other = &fg->nodes[k];
                const char* other_reads[MAX_READS_PER_PASS];
                size_t other_read_count = tc_pass_get_reads(other->pass, other_reads, MAX_READS_PER_PASS);

                for (size_t r = 0; r < other_read_count; r++) {
                    if (strcmp(other_reads[r], read_name) == 0) {
                        if (add_dependent(other, (int)i)) {
                            node->in_degree++;
                        }
                        break;
                    }
                }
            }
        }
    }

    return true;
}

static bool topological_sort(tc_frame_graph* fg) {
    fg->schedule = (tc_pass**)malloc(fg->node_count * sizeof(tc_pass*));
    fg->schedule_count = 0;

    int* in_degree = (int*)malloc(fg->node_count * sizeof(int));
    for (size_t i = 0; i < fg->node_count; i++) {
        in_degree[i] = fg->nodes[i].in_degree;
    }

    int* queue_normal = (int*)malloc(fg->node_count * sizeof(int));
    int* queue_inplace = (int*)malloc(fg->node_count * sizeof(int));
    size_t qn_head = 0, qn_tail = 0;
    size_t qi_head = 0, qi_tail = 0;

    for (size_t i = 0; i < fg->node_count; i++) {
        if (in_degree[i] == 0) {
            if (fg->nodes[i].is_inplace) {
                queue_inplace[qi_tail++] = (int)i;
            } else {
                queue_normal[qn_tail++] = (int)i;
            }
        }
    }

    while (qn_head < qn_tail || qi_head < qi_tail) {
        int idx;
        if (qn_head < qn_tail) {
            idx = queue_normal[qn_head++];
        } else {
            idx = queue_inplace[qi_head++];
        }

        fg->schedule[fg->schedule_count++] = fg->nodes[idx].pass;

        tc_fg_node* node = &fg->nodes[idx];
        for (size_t i = 0; i < node->dependent_count; i++) {
            int dep_idx = node->dependents[i];
            in_degree[dep_idx]--;

            if (in_degree[dep_idx] == 0) {
                if (fg->nodes[dep_idx].is_inplace) {
                    queue_inplace[qi_tail++] = dep_idx;
                } else {
                    queue_normal[qn_tail++] = dep_idx;
                }
            }
        }
    }

    free(in_degree);
    free(queue_normal);
    free(queue_inplace);

    if (fg->schedule_count != fg->node_count) {
        snprintf(fg->error_message, sizeof(fg->error_message), "Dependency cycle detected in frame graph");
        fg->error = TC_FG_ERROR_CYCLE;
        return false;
    }

    return true;
}

tc_frame_graph* tc_frame_graph_build(tc_pipeline_handle pipeline) {
    if (!tc_pipeline_pool_alive(pipeline)) return NULL;

    tc_frame_graph* fg = (tc_frame_graph*)calloc(1, sizeof(tc_frame_graph));
    if (!fg) return NULL;

    fg->error = TC_FG_OK;
    fg->error_message[0] = '\0';

    if (!build_dependency_graph(fg, pipeline)) {
        return fg;
    }
    if (!topological_sort(fg)) {
        return fg;
    }
    return fg;
}

void tc_frame_graph_destroy(tc_frame_graph* fg) {
    if (!fg) return;

    for (size_t i = 0; i < fg->resource_count; i++) {
        free(fg->resources[i].name);
        free(fg->resources[i].canonical);
    }
    for (size_t i = 0; i < fg->node_count; i++) {
        free(fg->nodes[i].dependents);
    }
    free(fg->schedule);
    free(fg);
}

tc_frame_graph_error tc_frame_graph_get_error(tc_frame_graph* fg) {
    return fg ? fg->error : TC_FG_OK;
}

const char* tc_frame_graph_get_error_message(tc_frame_graph* fg) {
    if (!fg || fg->error == TC_FG_OK) return NULL;
    return fg->error_message;
}

size_t tc_frame_graph_get_schedule(tc_frame_graph* fg, tc_pass** out_passes, size_t max_count) {
    if (!fg || !out_passes || fg->error != TC_FG_OK) return 0;
    size_t count = fg->schedule_count < max_count ? fg->schedule_count : max_count;
    memcpy(out_passes, fg->schedule, count * sizeof(tc_pass*));
    return count;
}

size_t tc_frame_graph_schedule_count(tc_frame_graph* fg) {
    return (fg && fg->error == TC_FG_OK) ? fg->schedule_count : 0;
}

tc_pass* tc_frame_graph_schedule_at(tc_frame_graph* fg, size_t index) {
    if (!fg || fg->error != TC_FG_OK || index >= fg->schedule_count) return NULL;
    return fg->schedule[index];
}

const char* tc_frame_graph_canonical_resource(tc_frame_graph* fg, const char* name) {
    if (!fg || !name) return name;
    tc_fg_resource* res = find_resource(fg, name);
    return res ? res->canonical : name;
}

size_t tc_frame_graph_get_alias_group(tc_frame_graph* fg, const char* canonical_name, const char** out_names, size_t max_count) {
    if (!fg || !canonical_name || !out_names) return 0;

    size_t count = 0;
    for (size_t i = 0; i < fg->resource_count && count < max_count; i++) {
        if (strcmp(fg->resources[i].canonical, canonical_name) == 0) {
            out_names[count++] = fg->resources[i].name;
        }
    }
    return count;
}

size_t tc_frame_graph_get_canonical_resources(tc_frame_graph* fg, const char** out_names, size_t max_count) {
    if (!fg || !out_names) return 0;

    size_t count = 0;
    for (size_t i = 0; i < fg->resource_count && count < max_count; i++) {
        const char* canonical = fg->resources[i].canonical;
        bool already_added = false;
        for (size_t j = 0; j < count; j++) {
            if (strcmp(out_names[j], canonical) == 0) {
                already_added = true;
                break;
            }
        }
        if (!already_added) {
            out_names[count++] = canonical;
        }
    }
    return count;
}

void tc_frame_graph_dump(tc_frame_graph* fg) {
    if (!fg) {
        tc_log(TC_LOG_INFO, "[tc_frame_graph] NULL");
        return;
    }
    if (fg->error != TC_FG_OK) {
        tc_log(TC_LOG_ERROR, "[tc_frame_graph] Error: %s", fg->error_message);
        return;
    }

    tc_log(TC_LOG_INFO, "[tc_frame_graph] Schedule (%zu passes):", fg->schedule_count);
    for (size_t i = 0; i < fg->schedule_count; i++) {
        tc_pass* p = fg->schedule[i];
        tc_log(TC_LOG_INFO, "  %zu: %s (%s)%s",
               i, p->pass_name, tc_pass_type_name(p),
               tc_pass_is_inplace(p) ? " [inplace]" : "");
    }

    tc_log(TC_LOG_INFO, "[tc_frame_graph] Resources (%zu):", fg->resource_count);
    for (size_t i = 0; i < fg->resource_count; i++) {
        tc_fg_resource* r = &fg->resources[i];
        if (strcmp(r->name, r->canonical) != 0) {
            tc_log(TC_LOG_INFO, "  %s -> %s (alias)", r->name, r->canonical);
        } else {
            tc_log(TC_LOG_INFO, "  %s", r->name);
        }
    }
}
