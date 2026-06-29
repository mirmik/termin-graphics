#include "guard_main.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tgfx2/backend_binding_plan.hpp"
#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/engine_shader_catalog.hpp"
#include "tgfx2/tc_shader_bridge.hpp"
#include "tgfx/resources/tc_shader_registry.h"

static void set_backend_env(const char* value) {
#ifdef _WIN32
    if (value) {
        _putenv_s("TERMIN_BACKEND", value);
    } else {
        _putenv_s("TERMIN_BACKEND", "");
    }
#else
    if (value) {
        setenv("TERMIN_BACKEND", value, 1);
    } else {
        unsetenv("TERMIN_BACKEND");
    }
#endif
}

static std::string read_test_text_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct ShaderRuntimeTestGuard {
    std::filesystem::path root;

    ~ShaderRuntimeTestGuard() {
        termin::tgfx2_set_shader_dev_compile_enabled(false);
        termin::tgfx2_set_shader_compiler_path("");
        termin::tgfx2_set_shader_cache_root("");
        termin::tgfx2_set_shader_artifact_root("");
        if (!root.empty()) {
            std::filesystem::remove_all(root);
        }
    }
};

static tc_shader_resource_binding make_plan_test_binding(
    const char* name,
    uint32_t kind,
    uint32_t scope,
    uint32_t binding,
    uint32_t stage_mask
) {
    tc_shader_resource_binding out{};
    std::snprintf(out.name, sizeof(out.name), "%s", name);
    out.kind = kind;
    out.scope = scope;
    out.set = 0;
    out.binding = binding;
    out.stage_mask = stage_mask;
    out.size = kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER ? 64u : 0u;
    return out;
}

TEST_CASE("tgfx2 device factory parses TERMIN_BACKEND aliases") {
    set_backend_env(nullptr);
    CHECK(tgfx::default_backend_from_env() == tgfx::compiled_default_backend());

    set_backend_env("opengl");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::OpenGL);

    set_backend_env("GL");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::OpenGL);

    set_backend_env("vk");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);

    set_backend_env("d3d11");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::D3D11);

    set_backend_env("DX11");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::D3D11);

    set_backend_env("definitely-not-a-backend");
    CHECK(tgfx::default_backend_from_env() == tgfx::compiled_default_backend());

    set_backend_env(nullptr);
}

TEST_CASE("tgfx2 shader artifact paths are backend aware") {
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_artifacts_" + std::to_string(unique));
    fs::create_directories(root / "shaders" / "vulkan");
    fs::create_directories(root / "shaders" / "opengl");
    fs::create_directories(root / "shaders" / "d3d11");

    const std::string root_str = root.generic_string();
    termin::tgfx2_set_shader_artifact_root(root_str.c_str());

    std::string path;
    CHECK(termin::tgfx2_shader_artifact_path(
        "shader-uuid",
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        path));
    CHECK(path == root_str + "/shaders/vulkan/shader-uuid.vert.spv");

    CHECK(termin::tgfx2_shader_artifact_path(
        "shader-uuid",
        tgfx::BackendType::OpenGL,
        tgfx::ShaderStage::Fragment,
        path));
    CHECK(path == root_str + "/shaders/opengl/shader-uuid.frag.glsl");

    CHECK(termin::tgfx2_shader_artifact_path(
        "shader-uuid",
        tgfx::BackendType::D3D11,
        tgfx::ShaderStage::Vertex,
        path));
    CHECK(path == root_str + "/shaders/d3d11/shader-uuid.vs.cso");

    const fs::path spirv_path = root / "shaders" / "vulkan" / "shader-uuid.frag.spv";
    {
        std::ofstream out(spirv_path, std::ios::binary);
        const char bytes[] = {'S', 'P', 'V'};
        out.write(bytes, sizeof(bytes));
    }

    std::vector<uint8_t> bytes;
    CHECK(termin::tgfx2_load_shader_artifact_for_backend(
        "shader-uuid",
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Fragment,
        bytes));
    CHECK(bytes.size() == 3);
    CHECK(bytes[0] == static_cast<uint8_t>('S'));
    CHECK(bytes[1] == static_cast<uint8_t>('P'));
    CHECK(bytes[2] == static_cast<uint8_t>('V'));

    termin::tgfx2_set_shader_artifact_root("");
    fs::remove_all(root);
}

TEST_CASE("backend binding plan separates semantic resources from backend placement") {
    tc_shader_resource_binding binding = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        1,
        TC_SHADER_STAGE_FRAGMENT);
    binding.has_d3d11_placement = 1;
    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    binding.d3d11.register_index = 3;

    tgfx::BackendBindingPlan vulkan_plan;
    std::string error;
    REQUIRE(tgfx::build_backend_binding_plan(
        tgfx::BackendType::Vulkan,
        &binding,
        1,
        vulkan_plan,
        &error));
    REQUIRE(vulkan_plan.entries.size() == 1);
    CHECK(vulkan_plan.entries[0].resource.name == "material");
    CHECK(vulkan_plan.entries[0].resource.kind == tgfx::ShaderResourceKind::ConstantBuffer);
    CHECK(vulkan_plan.entries[0].placement.kind == tgfx::BackendPlacementKind::VulkanDescriptor);
    CHECK(vulkan_plan.entries[0].placement.vulkan.set == 0u);
    CHECK(vulkan_plan.entries[0].placement.vulkan.binding == 1u);
    CHECK(vulkan_plan.entries[0].placement.vulkan.descriptor_kind ==
          tgfx::BackendDescriptorKind::UniformBuffer);

    tgfx::BackendBindingPlan d3d11_plan;
    REQUIRE(tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        &binding,
        1,
        d3d11_plan,
        &error));
    REQUIRE(d3d11_plan.entries.size() == 1);
    CHECK(d3d11_plan.entries[0].resource.name == "material");
    CHECK(d3d11_plan.entries[0].placement.kind == tgfx::BackendPlacementKind::D3D11Register);
    CHECK(d3d11_plan.entries[0].placement.d3d11.register_class == tgfx::D3D11RegisterClass::B);
    CHECK(d3d11_plan.entries[0].placement.d3d11.register_index == 3u);
    CHECK(!d3d11_plan.entries[0].placement.d3d11.scalar_sampler_for_texture_array);

    tgfx::BackendBindingPlan opengl_plan;
    REQUIRE(tgfx::build_backend_binding_plan(
        tgfx::BackendType::OpenGL,
        &binding,
        1,
        opengl_plan,
        &error));
    REQUIRE(opengl_plan.entries.size() == 1);
    CHECK(opengl_plan.entries[0].resource.name == "material");
    CHECK(opengl_plan.entries[0].placement.kind == tgfx::BackendPlacementKind::OpenGLBinding);
    CHECK(opengl_plan.entries[0].placement.opengl.binding_class ==
          tgfx::OpenGLBindingClass::UniformBuffer);
    CHECK(opengl_plan.entries[0].placement.opengl.binding_point == 1u);
}

TEST_CASE("bound resource groups are preferred over flat compatibility bindings") {
    tgfx::BackendBindingPlanEntry grouped_entry;
    grouped_entry.resource.name = "frame_data";
    grouped_entry.resource.kind = tgfx::ShaderResourceKind::ConstantBuffer;
    grouped_entry.resource.scope = tgfx::ShaderResourceScope::Frame;
    grouped_entry.stage_mask = TC_SHADER_STAGE_VERTEX;
    grouped_entry.placement.kind = tgfx::BackendPlacementKind::OpenGLBinding;
    grouped_entry.placement.opengl.binding_class =
        tgfx::OpenGLBindingClass::UniformBuffer;
    grouped_entry.placement.opengl.binding_point = 2;

    tgfx::BoundResourceValue grouped_value;
    grouped_value.kind = tgfx::BoundResourceKind::UniformBuffer;
    grouped_value.buffer = tgfx::BufferHandle{11};
    grouped_value.range = 64;

    tgfx::BoundResourceSetDesc grouped_desc;
    grouped_desc.resource_layout_token = 0x1234u;
    tgfx::BoundResourceGroup frame_group;
    frame_group.scope = tgfx::ShaderResourceScope::Frame;
    frame_group.bindings.push_back({grouped_entry, grouped_value});
    grouped_desc.groups.push_back(frame_group);

    tgfx::BackendBindingPlanEntry clean_entry = grouped_entry;
    clean_entry.resource.name = "material_data";
    clean_entry.resource.scope = tgfx::ShaderResourceScope::Material;
    clean_entry.placement.opengl.binding_point = 3;
    tgfx::BoundResourceGroup clean_group;
    clean_group.scope = tgfx::ShaderResourceScope::Material;
    clean_group.dirty = false;
    clean_group.bindings.push_back({clean_entry, grouped_value});
    grouped_desc.groups.push_back(clean_group);

    tgfx::BackendBindingPlanEntry ignored_flat_entry = grouped_entry;
    ignored_flat_entry.resource.name = "ignored_flat";
    ignored_flat_entry.placement.opengl.binding_point = 7;
    grouped_desc.bindings.push_back({ignored_flat_entry, grouped_value});

    tgfx::ResourceBinding legacy_numeric;
    legacy_numeric.kind = tgfx::ResourceBinding::Kind::UniformBuffer;
    legacy_numeric.binding = 4;
    legacy_numeric.buffer = tgfx::BufferHandle{22};
    legacy_numeric.range = 32;

    CHECK(tgfx::bound_resource_binding_count(grouped_desc) == 2u);
    CHECK(tgfx::dirty_bound_resource_binding_count(grouped_desc) == 1u);
    uint32_t dirty_binding_sum = 0;
    tgfx::for_each_dirty_bound_resource_binding(
        grouped_desc,
        [&](const tgfx::BoundResourceBinding& binding) {
            dirty_binding_sum += binding.plan_entry.placement.opengl.binding_point;
        });
    CHECK(dirty_binding_sum == 2u);

    tgfx::ResourceSetDesc legacy_desc =
        tgfx::legacy_resource_set_desc_from_bound(
            grouped_desc,
            std::vector<tgfx::ResourceBinding>{legacy_numeric});

    CHECK(legacy_desc.resource_layout_token == grouped_desc.resource_layout_token);
    CHECK(legacy_desc.descriptor_set_layout == grouped_desc.resource_layout_token);
    REQUIRE(legacy_desc.bindings.size() == 3u);
    CHECK(legacy_desc.bindings[0].binding == 4u);
    CHECK(legacy_desc.bindings[0].buffer == tgfx::BufferHandle{22});
    CHECK(legacy_desc.bindings[1].binding == 2u);
    CHECK(legacy_desc.bindings[1].buffer == tgfx::BufferHandle{11});
    CHECK(legacy_desc.bindings[1].range == 64u);
    CHECK(legacy_desc.bindings[2].binding == 3u);
}

TEST_CASE("backend binding plan validates D3D11 register class and stage conflicts") {
    tc_shader_resource_binding bindings[2]{};
    bindings[0] = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        1,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[0].has_d3d11_placement = 1;
    bindings[0].d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    bindings[0].d3d11.register_index = 1;

    bindings[1] = make_plan_test_binding(
        "draw_data",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        2,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[1].has_d3d11_placement = 1;
    bindings[1].d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    bindings[1].d3d11.register_index = 1;

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("conflict") != std::string::npos);

    bindings[1].stage_mask = TC_SHADER_STAGE_VERTEX;
    CHECK(tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        bindings,
        2,
        plan,
        &error));
    CHECK(plan.entries.size() == 2);

    bindings[1].d3d11.register_class = TC_SHADER_D3D11_REGISTER_T;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("register class") != std::string::npos);
}

TEST_CASE("backend binding plan validates missing D3D11 placement and class-separated slots") {
    tc_shader_resource_binding bindings[2]{};
    bindings[0] = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        2,
        TC_SHADER_STAGE_FRAGMENT);

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        bindings,
        1,
        plan,
        &error));
    CHECK(error.find("missing D3D11 placement") != std::string::npos);

    bindings[0].has_d3d11_placement = 1;
    bindings[0].d3d11.register_class = TC_SHADER_D3D11_REGISTER_B;
    bindings[0].d3d11.register_index = 2;

    bindings[1] = make_plan_test_binding(
        "albedo",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        2,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[1].has_d3d11_placement = 1;
    bindings[1].d3d11.register_class = TC_SHADER_D3D11_REGISTER_T;
    bindings[1].d3d11.register_index = 2;

    CHECK(tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        bindings,
        2,
        plan,
        &error));
    REQUIRE(plan.entries.size() == 2);
    CHECK(plan.entries[0].placement.d3d11.register_class == tgfx::D3D11RegisterClass::B);
    CHECK(plan.entries[1].placement.d3d11.register_class == tgfx::D3D11RegisterClass::T);
}

TEST_CASE("backend binding plan maps D3D11 storage buffers to shader-resource registers") {
    tc_shader_resource_binding binding = make_plan_test_binding(
        "foliage_instances",
        TC_SHADER_RESOURCE_STORAGE_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        43,
        TC_SHADER_STAGE_VERTEX);
    binding.has_d3d11_placement = 1;
    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_T;
    binding.d3d11.register_index = 5;

    tgfx::BackendBindingPlan plan;
    std::string error;
    REQUIRE(tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        &binding,
        1,
        plan,
        &error));
    REQUIRE(plan.entries.size() == 1);
    CHECK(plan.entries[0].resource.kind == tgfx::ShaderResourceKind::StorageBuffer);
    CHECK(plan.entries[0].placement.d3d11.register_class == tgfx::D3D11RegisterClass::T);
    CHECK(plan.entries[0].placement.d3d11.register_index == 5u);

    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_U;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        &binding,
        1,
        plan,
        &error));
    CHECK(error.find("register class") != std::string::npos);
}
TEST_CASE("backend binding plan rejects unsupported Vulkan descriptor sets") {
    tc_shader_resource_binding binding = make_plan_test_binding(
        "per_frame",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_FRAME,
        2,
        TC_SHADER_STAGE_VERTEX);
    binding.set = 1;

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::Vulkan,
        &binding,
        1,
        plan,
        &error));
    CHECK(error.find("set 0") != std::string::npos);
}

TEST_CASE("backend binding plan validates Vulkan and OpenGL placement conflicts") {
    tc_shader_resource_binding bindings[2]{};
    bindings[0] = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        1,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[1] = make_plan_test_binding(
        "draw_data",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_DRAW,
        1,
        TC_SHADER_STAGE_FRAGMENT);

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::OpenGL,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("OpenGL binding plan conflict") != std::string::npos);

    bindings[1] = make_plan_test_binding(
        "normal_map",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        1,
        TC_SHADER_STAGE_FRAGMENT);
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::Vulkan,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("Vulkan binding plan conflict") != std::string::npos);

    bindings[0] = make_plan_test_binding(
        "albedo",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        3,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[1] = make_plan_test_binding(
        "normal_map",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        3,
        TC_SHADER_STAGE_FRAGMENT);
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::OpenGL,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("OpenGL binding plan conflict") != std::string::npos);
}

TEST_CASE("backend binding plan rejects incompatible duplicate semantic names") {
    tc_shader_resource_binding bindings[2]{};
    bindings[0] = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_CONSTANT_BUFFER,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        1,
        TC_SHADER_STAGE_FRAGMENT);
    bindings[1] = make_plan_test_binding(
        "material",
        TC_SHADER_RESOURCE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_MATERIAL,
        2,
        TC_SHADER_STAGE_FRAGMENT);

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::Vulkan,
        bindings,
        2,
        plan,
        &error));
    CHECK(error.find("semantic conflict") != std::string::npos);
}

TEST_CASE("tc_shader records language and artifact policy") {
    const char* vs = "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
    const char* fs = "#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}";

    tc_shader_handle legacy = tc_shader_from_sources(
        vs,
        fs,
        nullptr,
        "legacy_shader_metadata_test",
        nullptr,
        "shader-metadata-legacy"
    );
    CHECK(!tc_shader_handle_is_invalid(legacy));
    tc_shader* legacy_shader = tc_shader_get(legacy);
    CHECK(legacy_shader != nullptr);
    CHECK(tc_shader_get_language(legacy_shader) == TC_SHADER_LANGUAGE_GLSL);
    CHECK(tc_shader_get_artifact_policy(legacy_shader) == TC_SHADER_ARTIFACT_OPTIONAL);
    CHECK(!tc_shader_requires_artifacts(legacy_shader));
    char legacy_hash[TC_SHADER_HASH_LEN];
    tc_shader_compute_hash(vs, fs, nullptr, legacy_hash);
    CHECK(std::string(legacy_shader->source_hash) == std::string(legacy_hash));
    CHECK(tc_shader_find_by_hash(legacy_hash).index == legacy.index);

    tc_shader_handle slang = tc_shader_from_sources_ex(
        vs,
        fs,
        nullptr,
        "slang_shader_metadata_test",
        nullptr,
        "shader-metadata-slang",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    );
    CHECK(!tc_shader_handle_is_invalid(slang));
    tc_shader* slang_shader = tc_shader_get(slang);
    CHECK(slang_shader != nullptr);
    CHECK(tc_shader_get_language(slang_shader) == TC_SHADER_LANGUAGE_SLANG);
    CHECK(tc_shader_get_artifact_policy(slang_shader) == TC_SHADER_ARTIFACT_REQUIRED);
    CHECK(tc_shader_requires_artifacts(slang_shader));

    const uint32_t slang_version = slang_shader->version;
    CHECK(!tc_shader_set_language(slang_shader, static_cast<tc_shader_language>(999)));
    CHECK(tc_shader_get_language(slang_shader) == TC_SHADER_LANGUAGE_SLANG);
    CHECK(slang_shader->version == slang_version);

    tc_shader_handle variant = tc_shader_from_sources(
        vs,
        fs,
        nullptr,
        "variant_shader_metadata_test",
        nullptr,
        "shader-metadata-variant"
    );
    CHECK(!tc_shader_handle_is_invalid(variant));
    tc_shader* variant_shader = tc_shader_get(variant);
    CHECK(variant_shader != nullptr);
    tc_shader_set_variant_info(variant_shader, slang, TC_SHADER_VARIANT_SKINNING);
    CHECK(tc_shader_get_language(variant_shader) == TC_SHADER_LANGUAGE_SLANG);
    CHECK(tc_shader_get_artifact_policy(variant_shader) == TC_SHADER_ARTIFACT_REQUIRED);

    tc_shader_handle required_variant = tc_shader_from_sources_ex(
        vs,
        fs,
        nullptr,
        "required_variant_metadata_test",
        nullptr,
        "shader-metadata-required-variant",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED);
    CHECK(!tc_shader_handle_is_invalid(required_variant));
    tc_shader* required_variant_shader = tc_shader_get(required_variant);
    REQUIRE(required_variant_shader != nullptr);
    tc_shader_set_variant_info(required_variant_shader, legacy, TC_SHADER_VARIANT_FOLIAGE);
    CHECK(tc_shader_get_language(required_variant_shader) == TC_SHADER_LANGUAGE_GLSL);
    CHECK(tc_shader_get_artifact_policy(required_variant_shader) == TC_SHADER_ARTIFACT_REQUIRED);

    tc_shader_destroy(required_variant);
    tc_shader_destroy(variant);
    tc_shader_destroy(slang);
    tc_shader_destroy(legacy);
}

TEST_CASE("static uuid registration preserves non-default shader identity") {
    const char* vs = "struct VOut { float4 position : SV_Position; };"
                     "[shader(\"vertex\")] VOut vs_main() {"
                     "VOut o; o.position = float4(0.0, 0.0, 0.0, 1.0); return o; }";
    const char* fs = "struct FOut { float4 color : SV_Target0; };"
                     "[shader(\"fragment\")] FOut fs_main() {"
                     "FOut o; o.color = float4(1.0, 1.0, 1.0, 1.0); return o; }";

    tc_shader_handle first = tc_shader_register_static_uuid_ex(
        vs,
        fs,
        nullptr,
        "static_slang_identity_test",
        "static-slang-identity-test",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED);
    REQUIRE(!tc_shader_handle_is_invalid(first));
    tc_shader* first_shader = tc_shader_get(first);
    REQUIRE(first_shader != nullptr);

    const uint32_t version = first_shader->version;
    const std::string source_hash = first_shader->source_hash;

    tc_shader_handle second = tc_shader_register_static_uuid_ex(
        vs,
        fs,
        nullptr,
        "static_slang_identity_test",
        "static-slang-identity-test",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED);
    CHECK(second.index == first.index);
    CHECK(second.generation == first.generation);

    tc_shader* second_shader = tc_shader_get(second);
    REQUIRE(second_shader != nullptr);
    CHECK(second_shader->version == version);
    CHECK(std::string(second_shader->source_hash) == source_hash);
    CHECK(tc_shader_get_language(second_shader) == TC_SHADER_LANGUAGE_SLANG);
    CHECK(tc_shader_get_artifact_policy(second_shader) == TC_SHADER_ARTIFACT_REQUIRED);
}

TEST_CASE("tc_shader identity hash separates source languages") {
    const char* vs = "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
    const char* fs = "#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}";

    tc_shader_handle glsl = tc_shader_from_sources_ex(
        vs,
        fs,
        nullptr,
        "identity_glsl",
        nullptr,
        nullptr,
        TC_SHADER_LANGUAGE_GLSL,
        TC_SHADER_ARTIFACT_OPTIONAL
    );
    tc_shader_handle slang = tc_shader_from_sources_ex(
        vs,
        fs,
        nullptr,
        "identity_slang",
        nullptr,
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    );

    CHECK(!tc_shader_handle_is_invalid(glsl));
    CHECK(!tc_shader_handle_is_invalid(slang));
    CHECK(glsl.index != slang.index);

    tc_shader* glsl_shader = tc_shader_get(glsl);
    tc_shader* slang_shader = tc_shader_get(slang);
    CHECK(glsl_shader != nullptr);
    CHECK(slang_shader != nullptr);
    CHECK(std::string(glsl_shader->source_hash) != std::string(slang_shader->source_hash));

    tc_shader_destroy(slang);
    tc_shader_destroy(glsl);
}

#ifndef _WIN32
TEST_CASE("tgfx2 shader runtime lazily compiles stale artifacts in dev mode") {
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string shader_uuid = "lazy-dev-compile-shader-" + std::to_string(unique);
    const fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_lazy_shader_" + std::to_string(unique));
    const fs::path artifact_root = root / "assets";
    const fs::path cache_root = root / "cache";
    const fs::path compiler = root / "fake_termin_shaderc.sh";
    fs::create_directories(root);
    ShaderRuntimeTestGuard runtime_config_guard{root};

    {
        std::ofstream out(compiler, std::ios::binary);
        out
            << "#!/bin/sh\n"
            << "printf '%s\\n' \"$@\" > \"$(dirname \"$0\")/compile_args.txt\"\n"
            << "out=''\n"
            << "while [ \"$#\" -gt 0 ]; do\n"
            << "  if [ \"$1\" = '--output' ]; then shift; out=\"$1\"; fi\n"
            << "  shift\n"
            << "done\n"
            << "if [ -z \"$out\" ]; then exit 9; fi\n"
            << "mkdir -p \"$(dirname \"$out\")\"\n"
            << "count_file=\"$(dirname \"$0\")/compile_count.txt\"\n"
            << "count=0\n"
            << "if [ -f \"$count_file\" ]; then count=$(cat \"$count_file\"); fi\n"
            << "count=$((count + 1))\n"
            << "printf '%s' \"$count\" > \"$count_file\"\n"
            << "printf 'SPIRV' > \"$out\"\n";
    }
    fs::permissions(
        compiler,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    const char* vs = R"(
struct VSOut {
    float4 position : SV_Position;
};
VSOut main(uint vertex_id : SV_VertexID) {
    VSOut outp;
    outp.position = float4(0.0, 0.0, 0.0, 1.0);
    return outp;
}
)";
    const char* fs_src = R"(
float4 main() : SV_Target {
    return float4(1.0, 0.0, 1.0, 1.0);
}
)";

    tc_shader_handle handle = tc_shader_from_sources_ex(
        vs,
        fs_src,
        nullptr,
        "lazy_dev_compile_shader",
        nullptr,
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    );
    CHECK(!tc_shader_handle_is_invalid(handle));
    tc_shader* shader = tc_shader_get(handle);
    CHECK(shader != nullptr);

    termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
    termin::tgfx2_set_shader_cache_root(cache_root.string().c_str());
    termin::tgfx2_set_shader_compiler_path(compiler.string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);

    std::vector<uint8_t> bytes;
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V'}));

    const std::string actual_shader_uuid = shader->uuid;
    const fs::path artifact = artifact_root / "shaders" / "vulkan"
        / (actual_shader_uuid + ".vert.spv");
    const fs::path metadata = fs::path(artifact.string() + ".artifact");
    const fs::path legacy_metadata = fs::path(artifact.string() + ".meta");
    const fs::path source = cache_root / "source"
        / (actual_shader_uuid + ".vert.slang");
    CHECK(fs::exists(artifact));
    CHECK(fs::exists(metadata));
    CHECK(!fs::exists(legacy_metadata));
    CHECK(fs::exists(source));
    CHECK(read_test_text_file(metadata).find("artifact_metadata_schema=1\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("layout_schema=4\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("shader_compiler=") != std::string::npos);

    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "1");
    CHECK(read_test_text_file(root / "compile_args.txt").find("--layout-scheme") == std::string::npos);
    CHECK(read_test_text_file(source).find("VSOut main") != std::string::npos);

    const auto compiler_mtime = fs::last_write_time(compiler);
    fs::last_write_time(compiler, compiler_mtime + std::chrono::seconds(1));
    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "2");

    tc_shader_destroy(handle);
}
#endif

#ifndef _WIN32
TEST_CASE("tgfx2 engine shader artifact metadata invalidates stale layout schema") {
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_engine_schema_" + std::to_string(unique));
    const fs::path artifact_root = root / "assets";
    const fs::path cache_root = root / "cache";
    const fs::path compiler = root / "fake_termin_shaderc.sh";
    fs::create_directories(root);
    ShaderRuntimeTestGuard runtime_config_guard{root};

    {
        std::ofstream out(compiler, std::ios::binary);
        out
            << "#!/bin/sh\n"
            << "out=''\n"
            << "while [ \"$#\" -gt 0 ]; do\n"
            << "  if [ \"$1\" = '--output' ]; then shift; out=\"$1\"; fi\n"
            << "  shift\n"
            << "done\n"
            << "if [ -z \"$out\" ]; then exit 9; fi\n"
            << "count_file='" << (root / "compile_count.txt").string() << "'\n"
            << "count=0\n"
            << "if [ -f \"$count_file\" ]; then count=$(cat \"$count_file\"); fi\n"
            << "count=$((count + 1))\n"
            << "printf '%s' \"$count\" > \"$count_file\"\n"
            << "mkdir -p \"$(dirname \"$out\")\"\n"
            << "printf 'SPIRV%s' \"$count\" > \"$out\"\n";
    }
    fs::permissions(
        compiler,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    const tgfx::EngineShaderStageSource& shader =
        tgfx::engine_fullscreen_quad_vertex_shader();
    const fs::path artifact = artifact_root / "shaders" / "vulkan"
        / (std::string(shader.uuid) + ".vert.spv");
    const fs::path metadata = fs::path(artifact.string() + ".artifact");

    termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
    termin::tgfx2_set_shader_cache_root(cache_root.string().c_str());
    termin::tgfx2_set_shader_compiler_path(compiler.string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);

    std::vector<uint8_t> bytes;
    REQUIRE(termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V', '1'}));
    CHECK(read_test_text_file(metadata).find("artifact_metadata_schema=1\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("layout_schema=4\n") != std::string::npos);

    {
        std::string stale_metadata = read_test_text_file(metadata);
        const std::string schema_line = "layout_schema=4\n";
        const size_t pos = stale_metadata.find(schema_line);
        REQUIRE(pos != std::string::npos);
        stale_metadata.erase(pos, schema_line.size());
        std::ofstream out(metadata, std::ios::binary);
        out << stale_metadata;
    }

    bytes.clear();
    REQUIRE(termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V', '2'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "2");
}
#endif

TEST_CASE("tgfx2 shader runtime loads D3D11 resource placement sidecar") {
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string shader_uuid = "d3d11-layout-sidecar-shader-" + std::to_string(unique);
    const fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_d3d11_layout_sidecar_" + std::to_string(unique));
    const fs::path artifact_root = root / "assets";
    const fs::path shader_dir = artifact_root / "shaders" / "d3d11";
    fs::create_directories(shader_dir);

    ShaderRuntimeTestGuard guard;
    guard.root = root;

    tc_shader_handle handle = tc_shader_from_sources_ex(
        "",
        "float4 main() : SV_Target0 { return 1; }",
        nullptr,
        "d3d11_layout_sidecar_shader",
        nullptr,
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    );
    CHECK(!tc_shader_handle_is_invalid(handle));
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    const fs::path artifact = shader_dir / (std::string(shader->uuid) + ".ps.cso");
    {
        std::ofstream out(artifact, std::ios::binary);
        out << "CSO";
    }
    {
        std::ofstream out(fs::path(artifact.string() + ".layout.json"), std::ios::binary);
        out
            << "{\n"
            << "  \"version\": 2,\n"
            << "  \"language\": \"slang\",\n"
            << "  \"target\": \"d3d11\",\n"
            << "  \"stage\": \"fragment\",\n"
            << "  \"resources\": [\n"
            << "    {\"name\": \"material\", \"kind\": \"constant_buffer\", "
            << "\"scope\": \"material\", \"set\": 0, \"binding\": 1, "
            << "\"stage_mask\": 2, \"size\": 16, "
            << "\"d3d11\": {\"register_class\": \"b\", \"register_index\": 1}},\n"
            << "    {\"name\": \"albedo_texture\", \"kind\": \"texture\", "
            << "\"scope\": \"material\", \"set\": 0, \"binding\": 4, "
            << "\"stage_mask\": 2, \"size\": 0, "
            << "\"d3d11\": {\"register_class\": \"t\", \"register_index\": 4, "
            << "\"scalar_sampler_for_texture_array\": true}}\n"
            << "  ]\n"
            << "}\n";
    }

    termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(false);

    std::vector<uint8_t> bytes;
    REQUIRE(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::D3D11,
        tgfx::ShaderStage::Fragment,
        bytes));

    const tc_shader_resource_binding* material =
        tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_MATERIAL);
    REQUIRE(material != nullptr);
    CHECK_EQ(material->kind, TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK_EQ(material->scope, TC_SHADER_RESOURCE_SCOPE_MATERIAL);
    CHECK_EQ(material->has_d3d11_placement, 1u);
    CHECK_EQ(material->d3d11.register_class, TC_SHADER_D3D11_REGISTER_B);
    CHECK_EQ(material->d3d11.register_index, 1u);

    const tc_shader_resource_binding* texture =
        tc_shader_find_resource_binding(shader, "albedo_texture");
    REQUIRE(texture != nullptr);
    CHECK_EQ(texture->kind, TC_SHADER_RESOURCE_TEXTURE);
    CHECK_EQ(texture->has_d3d11_placement, 1u);
    CHECK_EQ(texture->d3d11.register_class, TC_SHADER_D3D11_REGISTER_T);
    CHECK_EQ(texture->d3d11.register_index, 4u);
    CHECK_EQ(texture->d3d11_scalar_sampler_for_texture_array, 1u);

    tgfx::BackendBindingPlan plan;
    std::string error;
    REQUIRE(tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        tc_shader_resource_bindings(shader),
        tc_shader_resource_binding_count(shader),
        plan,
        &error));
    REQUIRE(plan.entries.size() == 2);
    const auto texture_entry = std::find_if(
        plan.entries.begin(),
        plan.entries.end(),
        [](const tgfx::BackendBindingPlanEntry& entry) {
            return entry.resource.name == "albedo_texture";
        });
    REQUIRE(texture_entry != plan.entries.end());
    CHECK(texture_entry->placement.d3d11.scalar_sampler_for_texture_array);

    tc_shader_destroy(handle);
}

#ifndef _WIN32
TEST_CASE("tgfx2 shader runtime loads resource layout sidecar") {
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string shader_uuid = "layout-sidecar-shader-" + std::to_string(unique);
    const fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_layout_sidecar_" + std::to_string(unique));
    const fs::path artifact_root = root / "assets";
    const fs::path cache_root = root / "cache";
    const fs::path compiler = root / "fake_termin_shaderc.sh";
    fs::create_directories(root);

    {
        std::ofstream out(compiler, std::ios::binary);
        out
            << "#!/bin/sh\n"
            << "out=''\n"
            << "while [ \"$#\" -gt 0 ]; do\n"
            << "  if [ \"$1\" = '--output' ]; then shift; out=\"$1\"; fi\n"
            << "  shift\n"
            << "done\n"
            << "if [ -z \"$out\" ]; then exit 9; fi\n"
            << "mkdir -p \"$(dirname \"$out\")\"\n"
            << "printf 'SPIRV' > \"$out\"\n"
            << "cat > \"$out.layout.json\" <<'JSON'\n"
            << "{\n"
            << "  \"version\": 1,\n"
            << "  \"language\": \"slang\",\n"
            << "  \"target\": \"vulkan\",\n"
            << "  \"stage\": \"fragment\",\n"
            << "  \"resources\": [\n"
            << "    {\"name\": \"material\", \"kind\": \"constant_buffer\", \"scope\": \"material\", \"set\": 2, \"binding\": 7, \"stage_mask\": 2, \"size\": 16}\n"
            << "  ]\n"
            << "}\n"
            << "JSON\n";
    }
    fs::permissions(
        compiler,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    const char* fs_src = R"(
float4 main() : SV_Target {
    return float4(1.0, 0.0, 1.0, 1.0);
}
)";

    tc_shader_handle handle = tc_shader_from_sources_ex(
        "",
        fs_src,
        nullptr,
        "layout_sidecar_shader",
        nullptr,
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    );
    CHECK(!tc_shader_handle_is_invalid(handle));
    tc_shader* shader = tc_shader_get(handle);
    REQUIRE(shader != nullptr);

    tc_material_ubo_entry entry{};
    std::snprintf(entry.name, sizeof(entry.name), "%s", "u_strength");
    std::snprintf(entry.property_type, sizeof(entry.property_type), "%s", "Float");
    entry.offset = 0;
    entry.size = 4;
    tc_shader_set_material_ubo_layout(shader, &entry, 1, 16);

    termin::tgfx2_set_shader_artifact_root(artifact_root.string().c_str());
    termin::tgfx2_set_shader_cache_root(cache_root.string().c_str());
    termin::tgfx2_set_shader_compiler_path(compiler.string().c_str());
    termin::tgfx2_set_shader_dev_compile_enabled(true);

    std::vector<uint8_t> bytes;
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Fragment,
        bytes));

    const tc_shader_resource_binding* binding =
        tc_shader_find_resource_binding(shader, TC_SHADER_RESOURCE_MATERIAL);
    REQUIRE(binding != nullptr);
    CHECK(binding->kind == TC_SHADER_RESOURCE_CONSTANT_BUFFER);
    CHECK(binding->scope == TC_SHADER_RESOURCE_SCOPE_MATERIAL);
    CHECK(binding->set == 2u);
    CHECK(binding->binding == 7u);
    CHECK(binding->size == 16u);
    CHECK((binding->stage_mask & TC_SHADER_STAGE_FRAGMENT) != 0u);

    termin::tgfx2_set_shader_dev_compile_enabled(false);
    termin::tgfx2_set_shader_compiler_path("");
    termin::tgfx2_set_shader_cache_root("");
    termin::tgfx2_set_shader_artifact_root("");
    tc_shader_destroy(handle);
    fs::remove_all(root);
}
#endif
