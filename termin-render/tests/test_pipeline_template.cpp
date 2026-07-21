#include "guard_main.h"

GUARD_TEST_MAIN();

#include <cstring>
#include <vector>

#include <termin/render/render_pipeline.hpp>
#include <termin/render/tc_pipeline_template.hpp>

extern "C" {
#include <render/tc_pipeline.h>
#include <render/tc_pipeline_template_registry.h>
}

static void keep_external_cache(void*) {}

TEST_CASE("compiled pipeline template round-trips without execution state") {
    tc_pipeline_template_init();
    const tc_pipeline_template_pass_desc passes[] = {
        {"DepthPass", "depth", "{\"bias\":0.001}", "main"},
        {"ForwardPass", "forward", "{\"phase\":\"opaque\"}", "main"},
    };
    const tc_pipeline_template_resource_desc resources[] = {
        {"scene-depth", "depth_texture", "D32_FLOAT", "main", 1920, 1080, 1.0f, 4, 7},
        {"scene-color", "color_texture", "RGBA16_FLOAT", "main", 0, 0, 0.5f, 1, 0},
    };
    const tc_pipeline_template_dependency_desc dependencies[] = {
        {0, "scene-depth", TC_PIPELINE_RESOURCE_WRITE},
        {1, "scene-depth", TC_PIPELINE_RESOURCE_READ},
        {1, "scene-color", TC_PIPELINE_RESOURCE_WRITE},
    };
    const tc_pipeline_template_target_desc targets[] = {
        {"main", "final-color", 1920, 1080},
    };
    const tc_pipeline_template_resource_view_desc resource_views[] = {
        {"scene-color.view", "scene-color", TC_PIPELINE_ATTACHMENT_COLOR},
        {"scene-depth.view", "scene-depth", TC_PIPELINE_ATTACHMENT_DEPTH},
    };
    const tc_pipeline_template_fbo_composition_desc fbo_compositions[] = {
        {"joined-scene", "scene-color.view", "scene-depth.view"},
    };
    const tc_pipeline_template_payload_desc payload = {
        TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION,
        "main pipeline",
        passes, 2,
        resources, 2,
        dependencies, 3,
        targets, 1,
        resource_views, 2,
        fbo_compositions, 1,
    };

    tc_pipeline_template_handle original_handle =
        tc_pipeline_template_create("pipeline-roundtrip-a", "placeholder");
    tc_pipeline_template* original = tc_pipeline_template_get(original_handle);
    REQUIRE(original != nullptr);
    const uint32_t initial_version = original->header.version;
    REQUIRE(tc_pipeline_template_set_payload(original, &payload));
    CHECK(tc_pipeline_template_version(original) == initial_version + 1);

    tc_pipeline_template_payload_desc rejected = payload;
    rejected.descriptor_version = TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION + 1;
    CHECK_FALSE(tc_pipeline_template_set_payload(original, &rejected));
    CHECK_EQ(tc_pipeline_template_version(original), initial_version + 1);
    CHECK_EQ(original->pass_count, 2u);

    const tc_pipeline_template_resource_view_desc invalid_views[] = {
        {"orphan", "missing-parent", TC_PIPELINE_ATTACHMENT_COLOR},
    };
    rejected = payload;
    rejected.resource_views = invalid_views;
    rejected.resource_view_count = 1;
    CHECK_FALSE(tc_pipeline_template_set_payload(original, &rejected));
    CHECK_EQ(tc_pipeline_template_version(original), initial_version + 1);
    CHECK_EQ(original->resource_view_count, 2u);

    const size_t byte_count = tc_pipeline_template_serialize(original, nullptr, 0);
    REQUIRE(byte_count > 32);
    std::vector<uint8_t> bytes(byte_count);
    std::vector<uint8_t> undersized(byte_count - 1, 0xa5);
    CHECK_EQ(
        tc_pipeline_template_serialize(original, undersized.data(), undersized.size()),
        byte_count);
    CHECK(undersized.front() == 0xa5);
    CHECK(tc_pipeline_template_serialize(original, bytes.data(), bytes.size()) == byte_count);
    CHECK(std::memcmp(bytes.data(), "TPLT", 4) == 0);

    tc_pipeline_template_handle decoded_handle = tc_pipeline_template_deserialize(
        "pipeline-roundtrip-b", bytes.data(), bytes.size());
    tc_pipeline_template* decoded = tc_pipeline_template_get(decoded_handle);
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
    REQUIRE(decoded->resource_view_count == 2);
    CHECK(std::strcmp(decoded->resource_views[0].parent, "scene-color") == 0);
    CHECK(decoded->resource_views[1].attachment == TC_PIPELINE_ATTACHMENT_DEPTH);
    REQUIRE(decoded->fbo_composition_count == 1);
    CHECK(std::strcmp(decoded->fbo_compositions[0].name, "joined-scene") == 0);
    CHECK(std::strcmp(decoded->fbo_compositions[0].depth, "scene-depth.view") == 0);

    CHECK(tc_pipeline_template_remove(original_handle));
    CHECK(tc_pipeline_template_remove(decoded_handle));
    tc_pipeline_template_shutdown();
}

TEST_CASE("instances share definition but isolate mutable execution state") {
    tc_pipeline_pool_init();
    tc_pipeline_template_handle template_handle =
        tc_pipeline_template_create("pipeline-shared", "shared");
    tc_pipeline_template* pipeline_template = tc_pipeline_template_get(template_handle);
    REQUIRE(pipeline_template != nullptr);
    tc_pipeline_template_retain(pipeline_template); // external strong handle

    tc_pipeline_handle first = tc_pipeline_create_from_template(template_handle);
    tc_pipeline_handle second = tc_pipeline_create_from_template(template_handle);
    REQUIRE(tc_pipeline_pool_alive(first));
    REQUIRE(tc_pipeline_pool_alive(second));
    CHECK(tc_pipeline_template_handle_eq(
        tc_pipeline_get_template(first), tc_pipeline_get_template(second)));
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
    CHECK(tc_pipeline_template_is_valid(template_handle));
    CHECK(pipeline_template->header.ref_count == 2); // external + second instance
    tc_pipeline_destroy(second);
    CHECK(tc_pipeline_template_is_valid(template_handle));
    CHECK(pipeline_template->header.ref_count == 1);
    CHECK(tc_pipeline_template_release(pipeline_template));
    CHECK_FALSE(tc_pipeline_template_is_valid(template_handle));

    tc_pipeline_template_handle replacement =
        tc_pipeline_template_create("pipeline-shared", "replacement");
    REQUIRE(tc_pipeline_template_is_valid(replacement));
    CHECK_FALSE(tc_pipeline_template_handle_eq(replacement, template_handle));
    CHECK(tc_pipeline_template_get(template_handle) == nullptr);
    CHECK(tc_pipeline_template_remove(replacement));
    tc_pipeline_pool_shutdown();
}

TEST_CASE("runtime-only pipeline does not synthesize a canonical template") {
    tc_pipeline_template_init();
    tc_pipeline_pool_init();

    const size_t template_count = tc_pipeline_template_count();
    const tc_pipeline_handle instance = tc_pipeline_create("runtime-only");
    REQUIRE(tc_pipeline_pool_alive(instance));
    CHECK(tc_pipeline_template_handle_is_invalid(tc_pipeline_get_template(instance)));
    CHECK(tc_pipeline_template_count() == template_count);

    tc_pipeline_destroy(instance);
    tc_pipeline_pool_shutdown();
    tc_pipeline_template_shutdown();
}

TEST_CASE("runtime instance restores resource views and FBO compositions from template") {
    tc_pipeline_pool_init();

    const tc_pipeline_template_resource_desc resources[] = {
        {"scene-fbo", "external", nullptr, nullptr, 0, 0, 1.0f, 1, 0},
    };
    const tc_pipeline_template_resource_view_desc resource_views[] = {
        {"scene-color", "scene-fbo", TC_PIPELINE_ATTACHMENT_COLOR},
        {"scene-depth", "scene-fbo", TC_PIPELINE_ATTACHMENT_DEPTH},
    };
    const tc_pipeline_template_fbo_composition_desc fbo_compositions[] = {
        {"joined-scene", "scene-color", "scene-depth"},
    };
    const tc_pipeline_template_payload_desc payload = {
        TC_PIPELINE_TEMPLATE_DESCRIPTOR_VERSION,
        "recipe pipeline",
        nullptr, 0,
        resources, 1,
        nullptr, 0,
        nullptr, 0,
        resource_views, 2,
        fbo_compositions, 1,
    };
    const tc_pipeline_template_handle handle =
        tc_pipeline_template_create("pipeline-recipe-instance", "placeholder");
    REQUIRE(tc_pipeline_template_set_payload(tc_pipeline_template_get(handle), &payload));

    {
        termin::TcPipelineTemplate definition(handle);
        termin::RenderPipeline instance(definition);
        REQUIRE(instance.is_valid());
        REQUIRE(instance.cache().resource_views.size() == 2);
        CHECK(instance.cache().resource_views.at("scene-color").parent == "scene-fbo");
        CHECK(
            instance.cache().resource_views.at("scene-depth").attachment
            == termin::AttachmentKind::Depth);
        REQUIRE(instance.cache().fbo_compositions.size() == 1);
        CHECK(instance.cache().fbo_compositions.at("joined-scene").color == "scene-color");
        CHECK(instance.cache().fbo_compositions.at("joined-scene").depth == "scene-depth");
        instance.destroy();
    }

    CHECK_FALSE(tc_pipeline_template_is_valid(handle));
    tc_pipeline_pool_shutdown();
}
