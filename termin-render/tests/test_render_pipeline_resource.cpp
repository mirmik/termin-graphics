#include "guard_main.h"

GUARD_TEST_MAIN();

#include <cstring>
#include <vector>

extern "C" {
#include <render/tc_pipeline.h>
#include <render/tc_render_pipeline_registry.h>
}

static void keep_external_cache(void*) {}

TEST_CASE("compiled pipeline descriptor round-trips without execution state") {
    tc_render_pipeline_init();
    const tc_render_pipeline_pass_desc passes[] = {
        {"DepthPass", "depth", "{\"bias\":0.001}", "main"},
        {"ForwardPass", "forward", "{\"phase\":\"opaque\"}", "main"},
    };
    const tc_render_pipeline_resource_desc resources[] = {
        {"scene-depth", "depth_texture", "D32_FLOAT", "main", 1920, 1080, 1.0f, 4, 7},
        {"scene-color", "color_texture", "RGBA16_FLOAT", "main", 0, 0, 0.5f, 1, 0},
    };
    const tc_render_pipeline_dependency_desc dependencies[] = {
        {0, "scene-depth", TC_PIPELINE_RESOURCE_WRITE},
        {1, "scene-depth", TC_PIPELINE_RESOURCE_READ},
        {1, "scene-color", TC_PIPELINE_RESOURCE_WRITE},
    };
    const tc_render_pipeline_target_desc targets[] = {
        {"main", "final-color", 1920, 1080},
    };
    const tc_render_pipeline_payload_desc payload = {
        TC_RENDER_PIPELINE_DESCRIPTOR_VERSION,
        "main pipeline",
        passes, 2,
        resources, 2,
        dependencies, 3,
        targets, 1,
    };

    tc_render_pipeline_handle original_handle =
        tc_render_pipeline_create("pipeline-roundtrip-a", "placeholder");
    tc_render_pipeline* original = tc_render_pipeline_get(original_handle);
    REQUIRE(original != nullptr);
    const uint32_t initial_version = original->header.version;
    REQUIRE(tc_render_pipeline_set_payload(original, &payload));
    CHECK(tc_render_pipeline_version(original) == initial_version + 1);

    tc_render_pipeline_payload_desc rejected = payload;
    rejected.descriptor_version = TC_RENDER_PIPELINE_DESCRIPTOR_VERSION + 1;
    CHECK_FALSE(tc_render_pipeline_set_payload(original, &rejected));
    CHECK_EQ(tc_render_pipeline_version(original), initial_version + 1);
    CHECK_EQ(original->pass_count, 2u);

    const size_t byte_count = tc_render_pipeline_serialize(original, nullptr, 0);
    REQUIRE(byte_count > 32);
    std::vector<uint8_t> bytes(byte_count);
    std::vector<uint8_t> undersized(byte_count - 1, 0xa5);
    CHECK_EQ(
        tc_render_pipeline_serialize(original, undersized.data(), undersized.size()),
        byte_count);
    CHECK(undersized.front() == 0xa5);
    CHECK(tc_render_pipeline_serialize(original, bytes.data(), bytes.size()) == byte_count);

    tc_render_pipeline_handle decoded_handle = tc_render_pipeline_deserialize(
        "pipeline-roundtrip-b", bytes.data(), bytes.size());
    tc_render_pipeline* decoded = tc_render_pipeline_get(decoded_handle);
    REQUIRE(decoded != nullptr);
    CHECK(std::strcmp(decoded->header.name, "main pipeline") == 0);
    REQUIRE(decoded->pass_count == 2);
    CHECK(std::strcmp(decoded->passes[0].name, "depth") == 0);
    CHECK(std::strcmp(decoded->passes[1].parameters, "{\"phase\":\"opaque\"}") == 0);
    REQUIRE(decoded->resource_count == 2);
    CHECK(decoded->resources[0].samples == 4);
    CHECK_EQ(decoded->resources[1].scale, 0.5f);
    REQUIRE(decoded->dependency_count == 3);
    CHECK(decoded->dependencies[1].access == TC_PIPELINE_RESOURCE_READ);
    REQUIRE(decoded->target_count == 1);
    CHECK(std::strcmp(decoded->targets[0].export_name, "final-color") == 0);

    CHECK(tc_render_pipeline_remove(original_handle));
    CHECK(tc_render_pipeline_remove(decoded_handle));
    tc_render_pipeline_shutdown();
}

TEST_CASE("instances share definition but isolate mutable execution state") {
    tc_pipeline_pool_init();
    tc_render_pipeline_handle resource_handle =
        tc_render_pipeline_create("pipeline-shared", "shared");
    tc_render_pipeline* resource = tc_render_pipeline_get(resource_handle);
    REQUIRE(resource != nullptr);
    tc_render_pipeline_retain(resource); // external strong handle

    tc_pipeline_handle first = tc_pipeline_create_from_resource(resource_handle);
    tc_pipeline_handle second = tc_pipeline_create_from_resource(resource_handle);
    REQUIRE(tc_pipeline_pool_alive(first));
    REQUIRE(tc_pipeline_pool_alive(second));
    CHECK(tc_render_pipeline_handle_eq(
        tc_pipeline_get_resource(first), tc_pipeline_get_resource(second)));
    CHECK(tc_pipeline_get_ptr(first) != tc_pipeline_get_ptr(second));
    REQUIRE(tc_pipeline_get_frame_graph(first) != nullptr);
    REQUIRE(tc_pipeline_get_frame_graph(second) != nullptr);
    CHECK(tc_pipeline_get_frame_graph(first) != tc_pipeline_get_frame_graph(second));

    int first_cache = 1;
    int second_cache = 2;
    tc_pipeline_set_render_cache(first, &first_cache, keep_external_cache);
    tc_pipeline_set_render_cache(second, &second_cache, keep_external_cache);
    CHECK(tc_pipeline_get_render_cache(first) == &first_cache);
    CHECK(tc_pipeline_get_render_cache(second) == &second_cache);
    tc_pipeline_mark_dirty(first);
    tc_pipeline_clear_dirty(second);
    CHECK(tc_pipeline_is_dirty(first));
    CHECK_FALSE(tc_pipeline_is_dirty(second));

    tc_pipeline_destroy(first);
    CHECK(tc_render_pipeline_is_valid(resource_handle));
    CHECK(resource->header.ref_count == 2); // external + second instance
    tc_pipeline_destroy(second);
    CHECK(tc_render_pipeline_is_valid(resource_handle));
    CHECK(resource->header.ref_count == 1);
    CHECK(tc_render_pipeline_release(resource));
    CHECK_FALSE(tc_render_pipeline_is_valid(resource_handle));

    tc_render_pipeline_handle replacement =
        tc_render_pipeline_create("pipeline-shared", "replacement");
    REQUIRE(tc_render_pipeline_is_valid(replacement));
    CHECK_FALSE(tc_render_pipeline_handle_eq(replacement, resource_handle));
    CHECK(tc_render_pipeline_get(resource_handle) == nullptr);
    CHECK(tc_render_pipeline_remove(replacement));
    tc_pipeline_pool_shutdown();
}
