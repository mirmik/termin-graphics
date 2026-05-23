#include "guard_main.h"

#include <cstdlib>

#include "tgfx2/device_factory.hpp"
#include "tgfx2/enums.hpp"

static void set_backend_env(const char* value) {
    if (value) {
        setenv("TERMIN_BACKEND", value, 1);
    } else {
        unsetenv("TERMIN_BACKEND");
    }
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

    set_backend_env("definitely-not-a-backend");
    CHECK(tgfx::default_backend_from_env() == tgfx::BackendType::Vulkan);

    set_backend_env(nullptr);
}
