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
#include "tgfx2/shader_artifact_resolver.hpp"
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

TEST_CASE("tgfx2 backend binding policy helpers keep stable mappings") {
    using tgfx::BackendBindingConflictClass;
    using tgfx::BackendType;
    using tgfx::ShaderResourceKind;
    using tgfx::ShaderResourceScope;

    CHECK(tgfx::shader_resource_kind_is_constant_buffer(ShaderResourceKind::ConstantBuffer));
    CHECK(!tgfx::shader_resource_kind_is_constant_buffer(ShaderResourceKind::StorageBuffer));
    CHECK(tgfx::shader_resource_kind_is_texture_like(ShaderResourceKind::Texture));
    CHECK(tgfx::shader_resource_kind_is_texture_like(ShaderResourceKind::Sampler));
    CHECK(tgfx::shader_resource_kind_is_texture_like(ShaderResourceKind::StorageTexture));
    CHECK(!tgfx::shader_resource_kind_is_texture_like(ShaderResourceKind::StorageBuffer));
    CHECK(tgfx::shader_resource_scope_has_transitional_binding_range(ShaderResourceScope::Frame));
    CHECK(tgfx::shader_resource_scope_has_transitional_binding_range(ShaderResourceScope::Transient));
    CHECK(!tgfx::shader_resource_scope_has_transitional_binding_range(ShaderResourceScope::Unscoped));
    CHECK(tgfx::stable_shader_resource_name_hash("hello") == 0x4f9f2cabu);

    const auto vulkan_constant = tgfx::transitional_backend_binding_range(
        BackendType::Vulkan, ShaderResourceKind::ConstantBuffer, ShaderResourceScope::Material);
    CHECK(vulkan_constant.base == 8);
    CHECK(vulkan_constant.size == 8);
    const auto opengl_texture = tgfx::transitional_backend_binding_range(
        BackendType::OpenGL, ShaderResourceKind::Texture, ShaderResourceScope::Pass);
    CHECK(opengl_texture.base == 20);
    CHECK(opengl_texture.size == 8);
    const auto d3d_storage = tgfx::transitional_backend_binding_range(
        BackendType::D3D11, ShaderResourceKind::StorageBuffer, ShaderResourceScope::Draw);
    CHECK(d3d_storage.base == 40);
    CHECK(d3d_storage.size == 16);
    const auto unscoped = tgfx::transitional_backend_binding_range(
        BackendType::Vulkan, ShaderResourceKind::Texture, ShaderResourceScope::Unscoped);
    CHECK(unscoped.base == 0);
    CHECK(unscoped.size == 0);

    CHECK(tgfx::backend_binding_conflict_class(BackendType::Vulkan, ShaderResourceKind::Texture) ==
          BackendBindingConflictClass::Descriptor);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::ConstantBuffer) ==
          BackendBindingConflictClass::ConstantBuffer);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::StorageBuffer) ==
          BackendBindingConflictClass::StorageBuffer);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::Texture) ==
          BackendBindingConflictClass::Texture);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::Sampler) ==
          BackendBindingConflictClass::Sampler);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::StorageTexture) ==
          BackendBindingConflictClass::StorageTexture);
    CHECK(tgfx::backend_binding_conflict_class(BackendType::OpenGL, ShaderResourceKind::None) ==
          BackendBindingConflictClass::None);
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

    set_backend_env("null");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Null);

    set_backend_env("definitely-not-a-backend");
    CHECK(tgfx::default_backend_from_env() == tgfx::compiled_default_backend());

    set_backend_env(nullptr);
}

TEST_CASE("tgfx2 backend names and compiled availability are queryable") {
    CHECK(std::string(tgfx::backend_name(tgfx::BackendType::OpenGL)) == "opengl");
    CHECK(std::string(tgfx::backend_name(tgfx::BackendType::Vulkan)) == "vulkan");
    CHECK(std::string(tgfx::backend_name(tgfx::BackendType::D3D11)) == "d3d11");

    CHECK(tgfx::backend_from_name("GL") == tgfx::BackendType::OpenGL);
    CHECK(tgfx::backend_from_name("vk") == tgfx::BackendType::Vulkan);
    CHECK(tgfx::backend_from_name("DX11") == tgfx::BackendType::D3D11);
    CHECK(tgfx::backend_from_name("definitely-not-a-backend") == tgfx::BackendType::Null);

    CHECK(tgfx::backend_is_compiled(tgfx::compiled_default_backend()));
    CHECK(!tgfx::backend_is_compiled(tgfx::BackendType::Null));
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

TEST_CASE("shader artifact resolvers isolate runtime roots") {
    termin::ShaderArtifactResolver first("/runtime/first", "/cache/first", "", false);
    termin::ShaderArtifactResolver second("/runtime/second", "/cache/second", "", false);

    std::string first_path;
    std::string second_path;
    REQUIRE(termin::tgfx2_shader_artifact_path(
        first,
        "shared-shader",
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Fragment,
        first_path
    ));
    REQUIRE(termin::tgfx2_shader_artifact_path(
        second,
        "shared-shader",
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Fragment,
        second_path
    ));

    CHECK(first_path == "/runtime/first/shaders/vulkan/shared-shader.frag.spv");
    CHECK(second_path == "/runtime/second/shaders/vulkan/shared-shader.frag.spv");
    CHECK(first.revision() == second.revision());
    first.set_artifact_root("/runtime/first-updated");
    CHECK(first.revision() != second.revision());
    CHECK(second.artifact_root() == "/runtime/second");
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

TEST_CASE("bound resource group views preserve dirty scopes and owned storage") {
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

    const tgfx::BoundResourceBinding frame_binding = {
        tgfx::bound_resource_slot_from_plan_entry(grouped_entry),
        grouped_value,
    };

    tgfx::BackendBindingPlanEntry clean_entry = grouped_entry;
    clean_entry.resource.name = "material_data";
    clean_entry.resource.scope = tgfx::ShaderResourceScope::Material;
    clean_entry.placement.opengl.binding_point = 3;
    const tgfx::BoundResourceBinding clean_binding = {
        tgfx::bound_resource_slot_from_plan_entry(clean_entry),
        grouped_value,
    };
    tgfx::BoundResourceSetStorage storage;
    storage.set_resource_layout_token(0x1234u);
    storage.append_group(
        tgfx::ShaderResourceScope::Frame, true, &frame_binding, 1);
    storage.append_group(
        tgfx::ShaderResourceScope::Material, false, &clean_binding, 1);
    const tgfx::BoundResourceSetDesc grouped_desc = storage.view();

    CHECK(tgfx::bound_resource_binding_count(grouped_desc) == 2u);
    CHECK(tgfx::dirty_bound_resource_binding_count(grouped_desc) == 1u);
    uint32_t dirty_binding_sum = 0;
    tgfx::for_each_dirty_bound_resource_binding(
        grouped_desc,
        [&](const tgfx::BoundResourceBinding& binding) {
            dirty_binding_sum += binding.slot.placement.opengl.binding_point;
        });
    CHECK(dirty_binding_sum == 2u);

    tgfx::BoundResourceBinding mutable_source = frame_binding;
    const tgfx::BoundResourceGroupView source_group = {
        tgfx::ShaderResourceScope::Frame, true, &mutable_source, 1,
    };
    const tgfx::BoundResourceSetDesc source_desc = {0x5678u, &source_group, 1};
    tgfx::BoundResourceSetStorage deferred_storage;
    deferred_storage.assign(source_desc);
    mutable_source.slot.placement.opengl.binding_point = 7;
    CHECK(deferred_storage.view().resource_layout_token == 0x5678u);
    CHECK(deferred_storage.view().groups[0].bindings[0].slot.placement.opengl.binding_point == 2u);
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

TEST_CASE("backend binding plan rejects D3D11 storage textures until UAV binding exists") {
    tc_shader_resource_binding binding = make_plan_test_binding(
        "output_image",
        TC_SHADER_RESOURCE_STORAGE_TEXTURE,
        TC_SHADER_RESOURCE_SCOPE_PASS,
        0,
        TC_SHADER_STAGE_FRAGMENT);
    binding.has_d3d11_placement = 1;
    binding.d3d11.register_class = TC_SHADER_D3D11_REGISTER_U;
    binding.d3d11.register_index = 0;

    tgfx::BackendBindingPlan plan;
    std::string error;
    CHECK(!tgfx::build_backend_binding_plan(
        tgfx::BackendType::D3D11,
        &binding,
        1,
        plan,
        &error));
    CHECK(error.find("storage texture") != std::string::npos);
    CHECK(error.find("D3D11") != std::string::npos);
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

    const tc_shader_create_desc slang_desc = {
        {vs, fs, nullptr, "slang_shader_metadata_test", nullptr, nullptr, nullptr, nullptr},
        "shader-metadata-slang",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle slang = tc_shader_from_sources_desc(&slang_desc);
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

    const tc_shader_create_desc required_variant_desc = {
        {vs, fs, nullptr, "required_variant_metadata_test", nullptr, nullptr, nullptr, nullptr},
        "shader-metadata-required-variant",
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle required_variant = tc_shader_from_sources_desc(&required_variant_desc);
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

TEST_CASE("retaining an assembled shader as static survives transient owner release") {
    const char* vs = "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
    const char* fs = "#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}";
    tc_shader_handle handle = tc_shader_from_sources(
        vs,
        fs,
        nullptr,
        "assembled_static_lifetime_test",
        nullptr,
        "assembled-static-lifetime-test");
    REQUIRE(!tc_shader_handle_is_invalid(handle));

    tc_shader* transient_owner = tc_shader_get(handle);
    REQUIRE(transient_owner != nullptr);
    tc_shader_add_ref(transient_owner);
    REQUIRE(tc_shader_retain_static(handle));
    const uint32_t retained_ref_count = transient_owner->ref_count;

    // Repeated promotion is idempotent, and releasing the temporary owner
    // leaves the registry's process-lifetime reference alive.
    REQUIRE(tc_shader_retain_static(handle));
    CHECK(transient_owner->ref_count == retained_ref_count);
    CHECK(!tc_shader_release(transient_owner));
    CHECK(tc_shader_is_valid(handle));
    CHECK(tc_shader_get(handle) != nullptr);
}

TEST_CASE("tc_shader identity hash separates source languages") {
    const char* vs = "#version 330 core\nvoid main(){gl_Position=vec4(0.0);}";
    const char* fs = "#version 330 core\nout vec4 c; void main(){c=vec4(1.0);}";

    const tc_shader_create_desc glsl_desc = {
        {vs, fs, nullptr, "identity_glsl", nullptr, nullptr, nullptr, nullptr},
        nullptr,
        TC_SHADER_LANGUAGE_GLSL,
        TC_SHADER_ARTIFACT_OPTIONAL
    };
    tc_shader_handle glsl = tc_shader_from_sources_desc(&glsl_desc);
    const tc_shader_create_desc slang_desc = {
        {vs, fs, nullptr, "identity_slang", nullptr, nullptr, nullptr, nullptr},
        nullptr,
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle slang = tc_shader_from_sources_desc(&slang_desc);

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
    const fs::path include_root = root / "include";
    const fs::path compiler = root / "fake_termin_shaderc.sh";
    fs::create_directories(include_root);
    ShaderRuntimeTestGuard runtime_config_guard{root};
    setenv("TERMIN_BUILTIN_SHADER_ROOT", include_root.string().c_str(), 1);

    {
        std::ofstream out(include_root / "test_direct.slang", std::ios::binary);
        out << "import test_transitive;\n";
    }
    {
        std::ofstream out(include_root / "test_transitive.slang", std::ios::binary);
        out << "static const float imported_value = 1.0;\n";
    }

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
            << "printf 'SPIRV' > \"$out\"\n"
            << "printf '{\"version\":1,\"resources\":[],\"compile_count\":%s}' \"$count\" > \"$out.layout.json\"\n";
    }
    fs::permissions(
        compiler,
        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
        fs::perm_options::add);

    const char* vs = R"(
import test_direct;
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

    const tc_shader_create_desc shader_desc = {
        {vs, fs_src, nullptr, "lazy_dev_compile_shader", nullptr, nullptr, nullptr, nullptr},
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle handle = tc_shader_from_sources_desc(&shader_desc);
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
    CHECK(read_test_text_file(metadata).find("artifact_metadata_schema=2\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("layout_schema=5\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("shader_compiler=") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("dependency_hash=") != std::string::npos);

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

    {
        std::ofstream out(include_root / "test_transitive.slang", std::ios::binary);
        out << "static const float imported_value = 2.0;\n";
    }
    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(read_test_text_file(root / "compile_count.txt") == "2");
    CHECK(read_test_text_file(fs::path(artifact.string() + ".layout.json")).find(
              "\"compile_count\":2") != std::string::npos);

    {
        std::ofstream out(include_root / "test_direct.slang", std::ios::binary);
        out << "import test_transitive;\n// direct dependency changed\n";
    }
    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(read_test_text_file(root / "compile_count.txt") == "3");

    fs::remove(include_root / "test_transitive.slang");
    bytes.clear();
    CHECK_FALSE(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes.empty());
    CHECK(read_test_text_file(root / "compile_count.txt") == "3");
    {
        std::ofstream out(include_root / "test_transitive.slang", std::ios::binary);
        out << "static const float imported_value = 2.0;\n";
    }

    const auto compiler_mtime = fs::last_write_time(compiler);
    fs::last_write_time(compiler, compiler_mtime + std::chrono::seconds(1));
    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "4");

    tc_shader_destroy(handle);
    unsetenv("TERMIN_BUILTIN_SHADER_ROOT");
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
    const fs::path include_root = root / "include";
    const fs::path compiler = root / "fake_termin_shaderc.sh";
    fs::create_directories(include_root);
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
    std::string source_filename = shader.source_resource_path;
    const std::string builtin_prefix = "builtin_shaders/";
    if (source_filename.rfind(builtin_prefix, 0) == 0) {
        source_filename.erase(0, builtin_prefix.size());
    }
    {
        std::ofstream out(include_root / source_filename, std::ios::binary);
        out << "import engine_direct;\nvoid main() {}\n";
    }
    {
        std::ofstream out(include_root / "engine_direct.slang", std::ios::binary);
        out << "import engine_transitive;\n";
    }
    {
        std::ofstream out(include_root / "engine_transitive.slang", std::ios::binary);
        out << "static const float engine_value = 1.0;\n";
    }
    setenv("TERMIN_BUILTIN_SHADER_ROOT", include_root.string().c_str(), 1);
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
    CHECK(read_test_text_file(metadata).find("artifact_metadata_schema=2\n") != std::string::npos);
    CHECK(read_test_text_file(metadata).find("layout_schema=5\n") != std::string::npos);

    {
        std::ofstream out(include_root / "engine_transitive.slang", std::ios::binary);
        out << "static const float engine_value = 2.0;\n";
    }
    bytes.clear();
    REQUIRE(termin::tgfx2_load_or_compile_engine_shader_stage_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V', '2'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "2");

    {
        std::string stale_metadata = read_test_text_file(metadata);
        const std::string schema_line = "layout_schema=5\n";
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
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V', '3'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "3");
    unsetenv("TERMIN_BUILTIN_SHADER_ROOT");
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

    const tc_shader_create_desc shader_desc = {
        {
            "",
            "float4 main() : SV_Target0 { return 1; }",
            nullptr,
            "d3d11_layout_sidecar_shader",
            nullptr,
            nullptr,
            nullptr,
            nullptr
        },
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle handle = tc_shader_from_sources_desc(&shader_desc);
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

    const tc_shader_create_desc shader_desc = {
        {"", fs_src, nullptr, "layout_sidecar_shader", nullptr, nullptr, nullptr, nullptr},
        shader_uuid.c_str(),
        TC_SHADER_LANGUAGE_SLANG,
        TC_SHADER_ARTIFACT_REQUIRED
    };
    tc_shader_handle handle = tc_shader_from_sources_desc(&shader_desc);
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
