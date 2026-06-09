#include "guard_main.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
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

TEST_CASE("tgfx2 device factory parses TERMIN_BACKEND aliases") {
    set_backend_env(nullptr);
#ifdef TGFX2_HAS_VULKAN
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);
#elif defined(TGFX2_HAS_OPENGL)
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::OpenGL);
#else
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Null);
#endif

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
#ifdef TGFX2_HAS_VULKAN
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);
#elif defined(TGFX2_HAS_OPENGL)
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::OpenGL);
#else
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Null);
#endif

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

    tc_shader_destroy(variant);
    tc_shader_destroy(slang);
    tc_shader_destroy(legacy);
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

TEST_CASE("tgfx2 shader runtime lazily compiles stale artifacts in dev mode") {
#ifdef _WIN32
    CHECK(true);
#else
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string shader_uuid = "lazy-dev-compile-shader-" + std::to_string(unique);
    const fs::path root = fs::temp_directory_path()
        / ("termin_tgfx2_lazy_shader_" + std::to_string(unique));
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

    const fs::path artifact = artifact_root / "shaders" / "vulkan"
        / (shader_uuid + ".vert.spv");
    const fs::path metadata = fs::path(artifact.string() + ".meta");
    const fs::path source = cache_root / "source"
        / (shader_uuid + ".vert.slang");
    CHECK(fs::exists(artifact));
    CHECK(fs::exists(metadata));
    CHECK(fs::exists(source));

    bytes.clear();
    CHECK(termin::tgfx2_load_or_compile_shader_artifact_for_backend(
        shader,
        tgfx::BackendType::Vulkan,
        tgfx::ShaderStage::Vertex,
        bytes));
    CHECK(bytes == std::vector<uint8_t>({'S', 'P', 'I', 'R', 'V'}));
    CHECK(read_test_text_file(root / "compile_count.txt") == "1");
    CHECK(read_test_text_file(source).find("VSOut main") != std::string::npos);

    termin::tgfx2_set_shader_dev_compile_enabled(false);
    termin::tgfx2_set_shader_compiler_path("");
    termin::tgfx2_set_shader_cache_root("");
    termin::tgfx2_set_shader_artifact_root("");
    tc_shader_destroy(handle);
    fs::remove_all(root);
#endif
}

TEST_CASE("tgfx2 shader runtime loads resource layout sidecar") {
#ifdef _WIN32
    CHECK(true);
#else
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
            << "    {\"name\": \"material\", \"kind\": \"constant_buffer\", \"set\": 2, \"binding\": 7, \"stage_mask\": 2, \"size\": 0}\n"
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
#endif
}
