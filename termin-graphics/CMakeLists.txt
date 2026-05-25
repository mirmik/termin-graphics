cmake_minimum_required(VERSION 3.16)
project(termin_graphics VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TGFX2_ENABLE_OPENGL "Enable OpenGL backend for tgfx2" ON)
if(ANDROID)
    set(_TGFX2_SHADERC_DEFAULT OFF)
else()
    set(_TGFX2_SHADERC_DEFAULT ON)
endif()
option(TGFX2_ENABLE_SHADERC "Enable runtime GLSL to SPIR-V compilation through shaderc" ${_TGFX2_SHADERC_DEFAULT})

if(TGFX2_ENABLE_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

# termin-base (provides tc_log shared logging)
if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(termin_mesh tmesh::termin_mesh)
else()
    find_package(termin_mesh REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

# --- GLAD (static library) ---
# Kept even when TGFX2_ENABLE_OPENGL=OFF: some legacy headers/bindings still
# mention GL-only APIs, and glad does not require linking system OpenGL.
add_library(glad STATIC
    third/glad/src/glad.c
)
if(COMMAND termin_add_alias_if_missing)
    termin_add_alias_if_missing(tgfx::glad glad)
endif()
target_include_directories(glad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third/glad/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# --- termin_graphics ---
set(TGFX_C_SOURCES
    src/tgfx_shader_preprocess.c
    src/resources/tc_shader_registry.c
    src/resources/tc_texture_registry.c
    src/resources/tc_material_registry.c
)
set(TGFX_CXX_SOURCES
    src/tgfx_texture_handle.cpp
)

add_library(termin_graphics SHARED ${TGFX_C_SOURCES} ${TGFX_CXX_SOURCES})
if(COMMAND termin_add_alias_if_missing)
    termin_add_alias_if_missing(tgfx::termin_graphics termin_graphics)
endif()
target_compile_definitions(termin_graphics PRIVATE TGFX_EXPORTS TC_EXPORTS)

if(MSVC)
    target_compile_options(termin_graphics PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(termin_graphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_graphics PUBLIC
    tcbase::termin_base
    tmesh::termin_mesh
)
if(TGFX2_ENABLE_OPENGL)
    target_link_libraries(termin_graphics PUBLIC glad OpenGL::GL)
else()
    target_link_libraries(termin_graphics PUBLIC glad)
endif()

# Set library version
set_target_properties(termin_graphics PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(termin_graphics)

# --- termin_graphics2 (new backend-neutral API) ---
set(TGFX2_SOURCES
    src/tgfx2/device_factory.cpp
    src/tgfx2/canvas2d_renderer.cpp
    src/tgfx2/font_atlas.cpp
    src/tgfx2/line_mesh_builder.cpp
    src/tgfx2/screen_space_line_renderer.cpp
    src/tgfx2/world_space_line_renderer.cpp
    src/tgfx2/text2d_renderer.cpp
    src/tgfx2/text3d_renderer.cpp
    src/tgfx2/pixel_format_utils.cpp
    src/tgfx2/pipeline_cache.cpp
    src/tgfx2/render_context.cpp
    src/tgfx2/render_runtime.cpp
    src/tgfx2_interop.cpp
    src/tgfx2/tc_mesh_bridge.cpp
    src/tgfx2/tc_shader_bridge.cpp
    src/tgfx2/texture_pool.cpp
)

if(TGFX2_ENABLE_OPENGL)
    list(APPEND TGFX2_SOURCES
        src/tgfx2/opengl/opengl_render_device.cpp
        src/tgfx2/opengl/opengl_command_list.cpp
        src/tgfx2/opengl/opengl_type_conversions.cpp
    )
endif()

# --- Optional Vulkan backend ---
# Auto-enabled when the Vulkan SDK is available on the host. Downstream
# packages (termin-display, termin-app) also auto-detect Vulkan and
# expect termin_graphics2 to carry the Vulkan backend code — if this
# option silently defaults to OFF on a Vulkan-capable host, libtermin_
# graphics2.so ships without VulkanRenderDevice symbols and any
# downstream link fails with "undefined reference to VulkanRenderDevice".
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    set(_TGFX2_VULKAN_DEFAULT ON)
else()
    set(_TGFX2_VULKAN_DEFAULT OFF)
endif()
option(TGFX2_ENABLE_VULKAN "Enable Vulkan backend for tgfx2" ${_TGFX2_VULKAN_DEFAULT})

if(TGFX2_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)

    set(TERMIN_VMA_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/vulkan-memory-allocator"
        CACHE PATH "Vulkan Memory Allocator source directory")
    if(NOT EXISTS "${TERMIN_VMA_DIR}/include/vk_mem_alloc.h")
        message(FATAL_ERROR
            "Vulkan Memory Allocator not found at ${TERMIN_VMA_DIR}. "
            "Run: git submodule update --init termin-thirdparty/vulkan-memory-allocator "
            "or set TERMIN_VMA_DIR.")
    endif()

    if(TGFX2_ENABLE_SHADERC)
        # shaderc lookup. Windows VulkanSDK ships three flavours and the
        # bare `shaderc.lib` is a trap — it's the static lib that needs
        # glslang/SPIRV-Tools/shaderc_util alongside, none of which we
        # link. Prefer `shaderc_combined` (single static blob, no DLL to
        # copy at runtime) and `shaderc_shared` (DLL stub) ahead of it.
        # Linux keeps the historical order: distro packages typically
        # install only `libshaderc_shared.so` under the name `shaderc`.
        if(WIN32)
            set(_SHADERC_NAMES shaderc_combined shaderc_shared shaderc)
        else()
            set(_SHADERC_NAMES shaderc shaderc_combined)
        endif()
        find_library(SHADERC_LIB NAMES ${_SHADERC_NAMES}
            HINTS "$ENV{VULKAN_SDK}" "${Vulkan_INCLUDE_DIR}/.."
                  "C:/VulkanSDK/1.4.341.1"
            PATH_SUFFIXES Lib lib lib64 x86_64/lib
        )
        if(NOT SHADERC_LIB)
            message(FATAL_ERROR
                "TGFX2_ENABLE_VULKAN and TGFX2_ENABLE_SHADERC are ON, but shaderc was not found. "
                "Install shaderc for the active Vulkan SDK, set VULKAN_SDK, "
                "or configure with -DTGFX2_ENABLE_SHADERC=OFF for SPIR-V-only Vulkan runtime."
            )
        endif()
    else()
        message(STATUS "tgfx2: runtime shaderc disabled; Vulkan accepts precompiled SPIR-V shaders")
    endif()

    list(APPEND TGFX2_SOURCES
        src/tgfx2/vulkan/vulkan_render_device.cpp
        src/tgfx2/vulkan/vulkan_command_list.cpp
        src/tgfx2/vulkan/vulkan_type_conversions.cpp
        src/tgfx2/vulkan/vulkan_shader_compiler.cpp
        src/tgfx2/vulkan/vulkan_swapchain.cpp
    )
endif()

add_library(termin_graphics2 SHARED ${TGFX2_SOURCES})
if(COMMAND termin_add_alias_if_missing)
    termin_add_alias_if_missing(tgfx::termin_graphics2 termin_graphics2)
endif()
target_compile_definitions(termin_graphics2 PRIVATE TGFX2_EXPORTS)

if(TGFX2_ENABLE_OPENGL)
    target_compile_definitions(termin_graphics2 PUBLIC TGFX2_HAS_OPENGL)
endif()

if(TGFX2_ENABLE_VULKAN)
    # PRIVATE on purpose: downstream consumers (termin-render,
    # termin-engine, termin-app) only touch tgfx2 through IRenderDevice
    # and never include tgfx2/vulkan/* headers. Keeping the Vulkan
    # target and TGFX2_HAS_VULKAN internal stops the SDK install from
    # requiring every consumer to `find_package(Vulkan)` themselves.
    target_compile_definitions(termin_graphics2 PRIVATE TGFX2_HAS_VULKAN)
    target_link_libraries(termin_graphics2 PRIVATE Vulkan::Vulkan)
    if(TGFX2_ENABLE_SHADERC)
        target_compile_definitions(termin_graphics2 PRIVATE TGFX2_HAS_SHADERC)
        target_link_libraries(termin_graphics2 PRIVATE ${SHADERC_LIB})
    endif()
    if(ANDROID)
        target_compile_definitions(termin_graphics2 PRIVATE VMA_VULKAN_VERSION=1000000)
    endif()
    target_include_directories(termin_graphics2 PRIVATE "${TERMIN_VMA_DIR}/include")
endif()

if(MSVC)
    target_compile_options(termin_graphics2 PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(termin_graphics2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# stb_truetype (vendored) — PRIVATE: only the font_atlas.cpp
# implementation needs it; the public header keeps stb out of its
# include surface by pimpl-wrapping stbtt_fontinfo.
target_include_directories(termin_graphics2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/third/stb
)

target_link_libraries(termin_graphics2 PUBLIC
    termin_graphics
    tmesh::termin_mesh
)
if(TGFX2_ENABLE_OPENGL)
    target_link_libraries(termin_graphics2 PUBLIC glad OpenGL::GL)
else()
    target_link_libraries(termin_graphics2 PUBLIC glad)
endif()

set_target_properties(termin_graphics2 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
if(COMMAND termin_enable_unity_build_for)
    termin_enable_unity_build_for(termin_graphics2)
endif()
if(COMMAND termin_enable_pch_for)
    termin_enable_pch_for(termin_graphics2)
endif()
termin_set_rpath_lib(termin_graphics2)

if(TGFX2_ENABLE_VULKAN AND TGFX2_ENABLE_SHADERC AND NOT ANDROID)
    add_executable(termin_shaderc tools/termin_shaderc.cpp)
    target_compile_features(termin_shaderc PRIVATE cxx_std_20)
    target_include_directories(termin_shaderc PRIVATE ${Vulkan_INCLUDE_DIR})
    target_link_libraries(termin_shaderc PRIVATE ${SHADERC_LIB})
    install(TARGETS termin_shaderc RUNTIME DESTINATION bin)
endif()

# --- Python bindings ---
option(TERMIN_BUILD_PYTHON "Build Python bindings" OFF)

if(TERMIN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

# --- Tests ---
option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)
    enable_testing()

    add_executable(tgfx_tests
        tests/main.cpp
        tests/tests_vertex_layout.cpp
        tests/tests_log.cpp
    )
    target_include_directories(tgfx_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard
    )
    target_link_libraries(tgfx_tests PRIVATE termin_graphics)

    add_test(NAME tgfx_tests COMMAND tgfx_tests)

    add_executable(tgfx_mesh_test tests/test_mesh.c)
    target_link_libraries(tgfx_mesh_test PRIVATE termin_graphics)
    add_test(NAME tgfx_mesh_test COMMAND tgfx_mesh_test)

    add_custom_target(termin_graphics_run_tests
        COMMAND tgfx_tests
        COMMAND tgfx_mesh_test
        DEPENDS tgfx_tests tgfx_mesh_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# --- tgfx2 tests ---
option(BUILD_TGFX2_TESTS "Build tgfx2 smoke tests" OFF)
option(BUILD_TGFX2_WINDOW_TESTS "Build tgfx2 tests that create windows/GL contexts" ON)

if(BUILD_TGFX2_TESTS)
    enable_testing()
    set(TGFX2_WINDOW_SKIP_RE
        "GLFW init failed|Failed to init GLFW|Window creation failed|Failed to create GLFW window|No available video device")

    if(BUILD_TGFX2_WINDOW_TESTS)
        find_package(glfw3 QUIET)
        if(NOT TARGET glfw)
            include(FetchContent)
            set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
            set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
            set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
            FetchContent_Declare(glfw
                GIT_REPOSITORY https://github.com/glfw/glfw.git
                GIT_TAG 3.4
            )
            FetchContent_MakeAvailable(glfw)
        endif()

        add_executable(tgfx2_smoke tests/test_tgfx2_smoke.cpp)
        target_link_libraries(tgfx2_smoke PRIVATE termin_graphics2 glfw)
        add_test(NAME tgfx2_smoke COMMAND tgfx2_smoke)
        set_tests_properties(tgfx2_smoke PROPERTIES
            TIMEOUT 10
            SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")

        add_executable(tgfx2_sdf_test tests/test_tgfx2_sdf.cpp)
        target_link_libraries(tgfx2_sdf_test PRIVATE termin_graphics2 glfw)
        add_test(NAME tgfx2_sdf_test COMMAND tgfx2_sdf_test)
        set_tests_properties(tgfx2_sdf_test PROPERTIES
            TIMEOUT 10
            SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")

        add_executable(tgfx2_render_context_test tests/test_render_context2.cpp)
        target_link_libraries(tgfx2_render_context_test PRIVATE termin_graphics2 glfw)
        add_test(NAME tgfx2_render_context_test COMMAND tgfx2_render_context_test)
        set_tests_properties(tgfx2_render_context_test PROPERTIES
            TIMEOUT 10
            SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
    endif()

    add_executable(tgfx2_device_factory_test
        tests/main.cpp
        tests/test_tgfx2_device_factory.cpp
        tests/test_tgfx2_line_mesh_builder.cpp
    )
    target_include_directories(tgfx2_device_factory_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard
    )
    target_link_libraries(tgfx2_device_factory_test PRIVATE termin_graphics2)
    add_test(NAME tgfx2_device_factory_test COMMAND tgfx2_device_factory_test)

    # Vulkan smoke test (no GLFW needed — offscreen)
    if(TGFX2_ENABLE_VULKAN)
        add_executable(tgfx2_vulkan_smoke tests/test_tgfx2_vulkan_smoke.cpp)
        target_compile_definitions(tgfx2_vulkan_smoke PRIVATE TGFX2_HAS_VULKAN)
        target_link_libraries(tgfx2_vulkan_smoke PRIVATE termin_graphics2 Vulkan::Vulkan)
        add_test(NAME tgfx2_vulkan_smoke COMMAND tgfx2_vulkan_smoke)
    endif()

    if(TGFX2_ENABLE_VULKAN AND BUILD_TGFX2_WINDOW_TESTS)
        # On-screen Vulkan smoke test. SDL2 lookup: Ubuntu's SDL2
        # ships `sdl2-config.cmake` (lowercase, non-standard) so we
        # skip find_package and go straight to find_path/find_library.
        set(SDL2_SMOKE_FOUND FALSE)
        find_path(SDL2_SMOKE_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
        find_library(SDL2_SMOKE_LIBRARY SDL2)
        if(SDL2_SMOKE_INCLUDE_DIR AND SDL2_SMOKE_LIBRARY)
            set(SDL2_SMOKE_FOUND TRUE)
            message(STATUS "tgfx2 Vulkan window smoke: SDL2 at ${SDL2_SMOKE_LIBRARY}")
        else()
            message(STATUS "tgfx2 Vulkan window smoke: SDL2 not found; skipping")
        endif()
        if(SDL2_SMOKE_FOUND)
            add_executable(tgfx2_vulkan_window tests/test_tgfx2_vulkan_window.cpp)
            target_include_directories(tgfx2_vulkan_window PRIVATE ${SDL2_SMOKE_INCLUDE_DIR})
            # TGFX2_HAS_VULKAN is PRIVATE on the lib (we don't want it
            # leaking into downstream SDK consumers); re-add it here
            # explicitly for the test translation unit.
            target_compile_definitions(tgfx2_vulkan_window PRIVATE TGFX2_HAS_VULKAN)
            target_link_libraries(tgfx2_vulkan_window
                PRIVATE termin_graphics2 Vulkan::Vulkan ${SDL2_SMOKE_LIBRARY})
            add_test(NAME tgfx2_vulkan_window COMMAND tgfx2_vulkan_window)
            set_tests_properties(tgfx2_vulkan_window PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
        endif()
    endif()
endif()

# --- Install ---
include(GNUInstallDirs)

set(TERMIN_GRAPHICS_INSTALL_TARGETS termin_graphics termin_graphics2 glad)

install(TARGETS ${TERMIN_GRAPHICS_INSTALL_TARGETS}
    EXPORT termin_graphicsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/tgfx include/tgfx2
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY third/glad/include/glad third/glad/include/KHR
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# --- CMake config for find_package ---
install(EXPORT termin_graphicsTargets
    FILE termin_graphicsTargets.cmake
    NAMESPACE tgfx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_graphics
)

include(CMakePackageConfigHelpers)

if(TGFX2_ENABLE_OPENGL)
    set(PACKAGE_TGFX2_ENABLE_OPENGL TRUE)
else()
    set(PACKAGE_TGFX2_ENABLE_OPENGL FALSE)
endif()

configure_package_config_file(
    cmake/termin_graphicsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_graphicsConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_graphics
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_graphicsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_graphicsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_graphicsConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_graphics
)
