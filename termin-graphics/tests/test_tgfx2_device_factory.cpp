#include "guard_main.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"
#include "tgfx2/tc_shader_bridge.hpp"

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
