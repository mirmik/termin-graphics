#include "guard_main.h"

#include <cstdlib>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"

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
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);

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
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);

    set_backend_env(nullptr);
}
