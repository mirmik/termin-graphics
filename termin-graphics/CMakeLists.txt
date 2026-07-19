cmake_minimum_required(VERSION 3.16)
project(termin_graphics VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TGFX2_ENABLE_OPENGL "Enable OpenGL backend for tgfx2" ON)
if(WIN32)
    set(_TGFX2_D3D11_DEFAULT ON)
else()
    set(_TGFX2_D3D11_DEFAULT OFF)
endif()
option(TGFX2_ENABLE_D3D11 "Enable Direct3D 11 backend for tgfx2" ${_TGFX2_D3D11_DEFAULT})
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
    ../termin-thirdparty/glad/src/glad.c
)
if(COMMAND termin_add_alias_if_missing)
    termin_add_alias_if_missing(tgfx::glad glad)
endif()
target_include_directories(glad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/glad/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# --- termin_graphics ---
set(TGFX_C_SOURCES
    src/resources/tc_shader_abi.c
    src/resources/tc_shader_registry.c
    src/resources/tc_shader_program_registry.c
    src/resources/tc_texture_registry.c
    src/resources/tc_material_registry.c
)
set(TGFX_CXX_SOURCES
    src/resources/tc_phase.cpp
    src/tgfx_texture_handle.cpp
)
if(TGFX2_ENABLE_OPENGL)
    list(APPEND TGFX_CXX_SOURCES src/tgfx_opengl_c_api.cpp)
endif()

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

# Shared by the runtime and the host-side shader compiler. Keeping it out of
# termin_graphics2 prevents the compiler used for SDK shader artifacts from
# depending on the library whose artifacts it generates.
add_library(termin_graphics_process_runner STATIC
    src/tgfx2/internal/process_runner.cpp
)
target_include_directories(termin_graphics_process_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --- termin_graphics2 (new backend-neutral API) ---
set(TGFX2_SOURCES
    src/tgfx2/builtin_shader_sources.cpp
    src/tgfx2/device_factory.cpp
    src/tgfx2/canvas2d_renderer.cpp
    src/tgfx2/engine_shader_catalog.cpp
    src/tgfx2/font_atlas.cpp
    src/tgfx2/immediate_renderer.cpp
    src/tgfx2/line_renderer_common.cpp
    src/tgfx2/line_mesh_builder.cpp
    src/tgfx2/screen_space_line_renderer.cpp
    src/tgfx2/world_space_line_renderer.cpp
    src/tgfx2/world_tube_line_renderer.cpp
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
    src/tgfx2/vertex_layout.cpp
)

# Binding policy is consumed both by the runtime library and by termin_shaderc.
# Keep it in a small shared library so the compiler can use the same exported
# policy without depending on termin_graphics2 and creating an artifact-build
# cycle.
add_library(termin_graphics_binding_policy SHARED
    src/tgfx2/backend_binding_plan.cpp
)
target_compile_definitions(termin_graphics_binding_policy PRIVATE TGFX2_EXPORTS)
target_include_directories(termin_graphics_binding_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_graphics_binding_policy PRIVATE termin_graphics)
termin_set_rpath_lib(termin_graphics_binding_policy)

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
        src/tgfx2/vulkan/vulkan_pipeline_resource_sets.cpp
        src/tgfx2/vulkan/vulkan_spirv_reflection.cpp
        src/tgfx2/vulkan/vulkan_type_conversions.cpp
        src/tgfx2/vulkan/vulkan_transfer.cpp
        src/tgfx2/vulkan/vulkan_shader_compiler.cpp
        src/tgfx2/vulkan/vulkan_stats.cpp
        src/tgfx2/vulkan/vulkan_swapchain.cpp
        src/tgfx2/vulkan/vulkan_tc_bridge.cpp
    )
endif()

if(TGFX2_ENABLE_D3D11)
    if(NOT WIN32)
        message(FATAL_ERROR "TGFX2_ENABLE_D3D11 requires Windows")
    endif()
    list(APPEND TGFX2_SOURCES
        src/tgfx2/d3d11/d3d11_render_device.cpp
        src/tgfx2/d3d11/d3d11_tc_bridge.cpp
        src/tgfx2/d3d11/d3d11_shader_reflection.cpp
        src/tgfx2/d3d11/d3d11_command_list.cpp
        src/tgfx2/d3d11/d3d11_swapchain.cpp
        src/tgfx2/d3d11/d3d11_type_conversions.cpp
    )
endif()

add_library(termin_graphics2 SHARED ${TGFX2_SOURCES})
if(COMMAND termin_add_alias_if_missing)
    termin_add_alias_if_missing(tgfx::termin_graphics2 termin_graphics2)
endif()
target_compile_definitions(termin_graphics2 PRIVATE
    TGFX2_EXPORTS
)

file(GLOB TERMIN_BUILTIN_SHADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_shaders/*"
)
set(TERMIN_BUILTIN_SHADER_BUILD_ROOT
    "${CMAKE_BINARY_DIR}/share/termin/builtin_shaders"
)
set(TERMIN_BUILTIN_SHADER_BUILD_STAMP
    "${CMAKE_BINARY_DIR}/share/termin/.builtin_shaders.stamp"
)
add_custom_command(
    OUTPUT "${TERMIN_BUILTIN_SHADER_BUILD_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${TERMIN_BUILTIN_SHADER_BUILD_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_shaders"
            "${TERMIN_BUILTIN_SHADER_BUILD_ROOT}"
    COMMAND ${CMAKE_COMMAND} -E touch "${TERMIN_BUILTIN_SHADER_BUILD_STAMP}"
    DEPENDS ${TERMIN_BUILTIN_SHADER_FILES}
    COMMENT "Staging built-in shaders in the build tree"
    VERBATIM
)
add_custom_target(termin_builtin_shaders_build_tree
    DEPENDS "${TERMIN_BUILTIN_SHADER_BUILD_STAMP}"
)
add_dependencies(termin_graphics2 termin_builtin_shaders_build_tree)

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
    # VMA is a vendored header-only dependency; keep its diagnostics out of
    # repository-owned warning/lint baselines.
    target_include_directories(termin_graphics2 SYSTEM PRIVATE "${TERMIN_VMA_DIR}/include")
endif()

if(TGFX2_ENABLE_D3D11)
    target_compile_definitions(termin_graphics2 PRIVATE TGFX2_HAS_D3D11)
    target_link_libraries(termin_graphics2 PRIVATE d3d11 dxgi d3d9 d3dcompiler)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/stb
)

target_link_libraries(termin_graphics2 PUBLIC
    termin_graphics
    tmesh::termin_mesh
    termin_graphics_binding_policy
)
target_link_libraries(termin_graphics2 PRIVATE termin_graphics_process_runner)
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

if((TGFX2_ENABLE_D3D11 OR (TGFX2_ENABLE_VULKAN AND TGFX2_ENABLE_SHADERC)) AND NOT ANDROID)
    add_executable(termin_shaderc
        tools/termin_shaderc.cpp
        tools/termin_shaderc/compiler_support.cpp
        tools/termin_shaderc/file_io.cpp
        tools/termin_shaderc/backend_patchers.cpp
        tools/termin_shaderc/cli.cpp
    )
    target_compile_features(termin_shaderc PRIVATE cxx_std_20)
    target_include_directories(termin_shaderc PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
    # The shader compiler must not depend on termin_graphics2: it produces
    # the built-in artifacts required by that shared library. Binding policy
    # lives in its own shared library so both targets use the same mapping.
    target_link_libraries(termin_shaderc PRIVATE
        termin_base
        termin_graphics
        termin_graphics_binding_policy
        termin_graphics_process_runner
    )
    if(TGFX2_ENABLE_VULKAN AND TGFX2_ENABLE_SHADERC)
        target_compile_definitions(termin_shaderc PRIVATE TERMIN_SHADERC_HAS_SHADERC)
        target_include_directories(termin_shaderc PRIVATE ${Vulkan_INCLUDE_DIR})
        target_link_libraries(termin_shaderc PRIVATE ${SHADERC_LIB})
    endif()
    termin_set_rpath_tool(termin_shaderc)
    install(TARGETS termin_shaderc RUNTIME DESTINATION bin)
endif()

if(WIN32 AND TGFX2_ENABLE_D3D11 AND NOT ANDROID)
    set(_TERMIN_BUILTIN_SHADER_ARTIFACTS_DEFAULT ON)
else()
    set(_TERMIN_BUILTIN_SHADER_ARTIFACTS_DEFAULT OFF)
endif()
option(TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS
    "Compile built-in shader artifacts into the SDK share/termin tree"
    ${_TERMIN_BUILTIN_SHADER_ARTIFACTS_DEFAULT})

if(TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS)
    if(NOT TARGET termin_shaderc)
        message(FATAL_ERROR
            "TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=ON requires the termin_shaderc target. "
            "Enable D3D11 or Vulkan+shaderc, or disable TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS.")
    endif()
    if(NOT Python_EXECUTABLE)
        find_package(Python 3.10 COMPONENTS Interpreter REQUIRED)
    endif()

    set(_termin_builtin_shader_artifact_targets "")
    if(WIN32 AND TGFX2_ENABLE_D3D11)
        list(APPEND _termin_builtin_shader_artifact_targets d3d11)
    endif()
    if(NOT _termin_builtin_shader_artifact_targets)
        message(FATAL_ERROR
            "TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=ON but no built-in shader artifact targets are enabled")
    endif()

    set(_termin_builtin_shader_slangc "")
    if(DEFINED ENV{TERMIN_SLANGC} AND EXISTS "$ENV{TERMIN_SLANGC}")
        set(_termin_builtin_shader_slangc "$ENV{TERMIN_SLANGC}")
    elseif(WIN32 AND DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}/Bin/slangc.exe")
        set(_termin_builtin_shader_slangc "$ENV{VULKAN_SDK}/Bin/slangc.exe")
    else()
        find_program(_termin_builtin_shader_slangc NAMES slangc HINTS "$ENV{VULKAN_SDK}/Bin")
    endif()
    if(NOT _termin_builtin_shader_slangc)
        message(FATAL_ERROR
            "TERMIN_BUILD_BUILTIN_SHADER_ARTIFACTS=ON requires slangc. "
            "Set TERMIN_SLANGC or add slangc to PATH.")
    endif()

    set(_termin_builtin_shader_artifact_args
        "--shaderc" "$<TARGET_FILE:termin_shaderc>"
        "--slangc" "${_termin_builtin_shader_slangc}"
        "--source-dir" "${CMAKE_CURRENT_SOURCE_DIR}/resources/builtin_shaders"
        "--output-root" "${CMAKE_BINARY_DIR}/share/termin"
    )
    foreach(_target IN LISTS _termin_builtin_shader_artifact_targets)
        list(APPEND _termin_builtin_shader_artifact_args "--target" "${_target}")
    endforeach()

    if(WIN32)
        set(_termin_builtin_shader_fxc "")
        if(DEFINED ENV{TERMIN_FXC} AND EXISTS "$ENV{TERMIN_FXC}")
            set(_termin_builtin_shader_fxc "$ENV{TERMIN_FXC}")
        else()
            find_program(_termin_builtin_shader_fxc NAMES fxc)
        endif()
        if(_termin_builtin_shader_fxc)
            list(APPEND _termin_builtin_shader_artifact_args "--fxc" "${_termin_builtin_shader_fxc}")
        endif()
    endif()

    set(_termin_builtin_shader_artifact_script
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_builtin_shader_artifacts.py")
    set(_termin_builtin_shader_artifact_stamp
        "${CMAKE_BINARY_DIR}/share/termin/.builtin_shader_artifacts.stamp")
    add_custom_command(
        OUTPUT "${_termin_builtin_shader_artifact_stamp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/share/termin"
        COMMAND "${Python_EXECUTABLE}" "${_termin_builtin_shader_artifact_script}"
                ${_termin_builtin_shader_artifact_args}
        COMMAND ${CMAKE_COMMAND} -E touch "${_termin_builtin_shader_artifact_stamp}"
        DEPENDS
            termin_shaderc
            "${_termin_builtin_shader_artifact_script}"
            ${TERMIN_BUILTIN_SHADER_FILES}
        COMMENT "Compiling built-in shader artifacts"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )
    add_custom_target(termin_builtin_shader_artifacts ALL
        DEPENDS "${_termin_builtin_shader_artifact_stamp}")
    add_dependencies(termin_graphics2 termin_builtin_shader_artifacts)
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
        tests/tests_tc_texture.cpp
        tests/tests_vertex_layout.cpp
        tests/tests_log.cpp
        tests/tests_phase.cpp
        tests/tests_shader_resource_layout.cpp
        tests/tests_shader_program.cpp
    )
    target_include_directories(tgfx_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard
    )
    target_link_libraries(tgfx_tests PRIVATE termin_graphics)
    termin_set_rpath_test(tgfx_tests)

    add_test(NAME tgfx_tests COMMAND tgfx_tests)

    add_executable(tgfx_mesh_test tests/test_mesh.c)
    target_link_libraries(tgfx_mesh_test PRIVATE termin_graphics)
    termin_set_rpath_test(tgfx_mesh_test)
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
option(BUILD_TGFX2_D3D11_WINDOW_TEST "Build tgfx2 D3D11 SDL/DXGI window smoke test" ${BUILD_TGFX2_WINDOW_TESTS})

if(BUILD_TGFX2_TESTS)
    enable_testing()
    set(TGFX2_WINDOW_SKIP_RE
        "GLFW init failed|Failed to init GLFW|Window creation failed|Failed to create GLFW window|No available video device|Vulkan support is either not configured in SDL")
    set(TGFX2_VULKAN_RUNTIME_SKIP_RE
        "Failed to create Vulkan device: Failed to create Vulkan instance|Failed to create Vulkan device: No Vulkan-capable GPU found")

    add_executable(tgfx2_front_face_contract_test
        tests/test_tgfx2_front_face_contract.cpp)
    target_link_libraries(tgfx2_front_face_contract_test PRIVATE termin_graphics2)
    target_sources(tgfx2_front_face_contract_test PRIVATE src/tgfx2/opengl/opengl_type_conversions.cpp)
    if(TGFX2_ENABLE_VULKAN)
        target_compile_definitions(tgfx2_front_face_contract_test PRIVATE TGFX2_HAS_VULKAN)
        target_link_libraries(tgfx2_front_face_contract_test PRIVATE Vulkan::Vulkan)
    endif()
    if(TGFX2_ENABLE_D3D11)
        target_compile_definitions(tgfx2_front_face_contract_test PRIVATE TGFX2_HAS_D3D11)
        target_sources(tgfx2_front_face_contract_test PRIVATE src/tgfx2/d3d11/d3d11_type_conversions.cpp)
    endif()
    termin_set_rpath_test(tgfx2_front_face_contract_test)
    add_test(NAME tgfx2_front_face_contract_test COMMAND tgfx2_front_face_contract_test)
    set_tests_properties(tgfx2_front_face_contract_test PROPERTIES TIMEOUT 10)

    add_executable(tgfx2_sdf_test tests/test_tgfx2_sdf.cpp)
    target_compile_definitions(tgfx2_sdf_test PRIVATE
        TGFX2_TEST_FONT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf")
    target_link_libraries(tgfx2_sdf_test PRIVATE termin_graphics2)
    termin_set_rpath_test(tgfx2_sdf_test)
    add_test(NAME tgfx2_sdf_test COMMAND tgfx2_sdf_test)
    set_tests_properties(tgfx2_sdf_test PROPERTIES TIMEOUT 10)

    if(BUILD_TGFX2_WINDOW_TESTS)
        find_package(glfw3 CONFIG QUIET)
        if(TARGET glfw)
            set(TERMIN_TGFX2_GLFW_AVAILABLE TRUE CACHE INTERNAL
                "Whether the active build environment provides GLFW")

            add_executable(tgfx2_smoke tests/test_tgfx2_smoke.cpp)
            target_link_libraries(tgfx2_smoke PRIVATE termin_graphics2 glfw)
            termin_set_rpath_test(tgfx2_smoke)
            add_test(NAME tgfx2_smoke COMMAND tgfx2_smoke)
            set_tests_properties(tgfx2_smoke PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
            termin_add_test_labels(tgfx2_smoke
                "termin:capability:window"
                "termin:capability:glfw")

            add_executable(tgfx2_render_context_test tests/test_render_context2.cpp)
            target_link_libraries(tgfx2_render_context_test PRIVATE termin_graphics2 glfw)
            termin_set_rpath_test(tgfx2_render_context_test)
            add_test(NAME tgfx2_render_context_test COMMAND tgfx2_render_context_test)
            set_tests_properties(tgfx2_render_context_test PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
            termin_add_test_labels(tgfx2_render_context_test
                "termin:capability:window"
                "termin:capability:glfw")
        else()
            set(TERMIN_TGFX2_GLFW_AVAILABLE FALSE CACHE INTERNAL
                "Whether the active build environment provides GLFW")
            message(STATUS
                "tgfx2 GLFW window tests: GLFW was not found in the build environment; skipping")
        endif()
    endif()

    add_executable(tgfx2_device_factory_test
        tests/main.cpp
        tests/test_tgfx2_device_factory.cpp
        tests/test_tgfx2_interop_ownership.cpp
        tests/test_tgfx2_line_mesh_builder.cpp
        tests/test_tgfx2_pipeline_cache_stats.cpp
        tests/test_tgfx2_tc_mesh_bridge.cpp
    )
    target_include_directories(tgfx2_device_factory_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard
    )
    target_link_libraries(tgfx2_device_factory_test PRIVATE termin_graphics2)
    if(TGFX2_ENABLE_VULKAN)
        target_compile_definitions(tgfx2_device_factory_test PRIVATE TGFX2_HAS_VULKAN)
    endif()
    termin_set_rpath_test(tgfx2_device_factory_test)
    add_test(NAME tgfx2_device_factory_test COMMAND tgfx2_device_factory_test)

    if(TGFX2_ENABLE_OPENGL AND BUILD_TGFX2_WINDOW_TESTS)
        set(SDL2_OPENGL_SMOKE_FOUND FALSE)
        find_path(SDL2_OPENGL_SMOKE_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
        find_library(SDL2_OPENGL_SMOKE_LIBRARY SDL2)
        if(WIN32 AND SDL2_OPENGL_SMOKE_LIBRARY)
            get_filename_component(SDL2_OPENGL_SMOKE_LIBRARY_DIR
                "${SDL2_OPENGL_SMOKE_LIBRARY}" DIRECTORY)
            find_file(SDL2_OPENGL_SMOKE_DLL SDL2.dll
                PATHS
                    "${SDL2_OPENGL_SMOKE_LIBRARY_DIR}"
                    "${SDL2_OPENGL_SMOKE_LIBRARY_DIR}/../bin"
                NO_DEFAULT_PATH)
        endif()
        if(SDL2_OPENGL_SMOKE_INCLUDE_DIR AND SDL2_OPENGL_SMOKE_LIBRARY)
            set(SDL2_OPENGL_SMOKE_FOUND TRUE)
            message(STATUS "tgfx2 OpenGL bound resource set smoke: SDL2 at ${SDL2_OPENGL_SMOKE_LIBRARY}")
        else()
            message(STATUS "tgfx2 OpenGL bound resource set smoke: SDL2 not found; skipping")
        endif()
        if(SDL2_OPENGL_SMOKE_FOUND)
            add_executable(tgfx2_opengl_bound_resource_set
                tests/test_tgfx2_opengl_bound_resource_set.cpp)
            target_include_directories(tgfx2_opengl_bound_resource_set
                PRIVATE ${SDL2_OPENGL_SMOKE_INCLUDE_DIR})
            target_link_libraries(tgfx2_opengl_bound_resource_set
                PRIVATE termin_graphics2 ${SDL2_OPENGL_SMOKE_LIBRARY})
            if(SDL2_OPENGL_SMOKE_DLL)
                add_custom_command(TARGET tgfx2_opengl_bound_resource_set POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SDL2_OPENGL_SMOKE_DLL}"
                        "$<TARGET_FILE_DIR:tgfx2_opengl_bound_resource_set>")
            endif()
            termin_set_rpath_test(tgfx2_opengl_bound_resource_set)
            add_test(NAME tgfx2_opengl_bound_resource_set
                COMMAND tgfx2_opengl_bound_resource_set)
            set_tests_properties(tgfx2_opengl_bound_resource_set PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
            termin_add_test_labels(tgfx2_opengl_bound_resource_set
                "termin:capability:window"
                "termin:capability:opengl")

            add_executable(tgfx2_opengl_navmesh_overlay
                tests/test_tgfx2_opengl_navmesh_overlay.cpp)
            target_include_directories(tgfx2_opengl_navmesh_overlay
                PRIVATE ${SDL2_OPENGL_SMOKE_INCLUDE_DIR})
            target_link_libraries(tgfx2_opengl_navmesh_overlay
                PRIVATE termin_graphics2 ${SDL2_OPENGL_SMOKE_LIBRARY})
            if(SDL2_OPENGL_SMOKE_DLL)
                add_custom_command(TARGET tgfx2_opengl_navmesh_overlay POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SDL2_OPENGL_SMOKE_DLL}"
                        "$<TARGET_FILE_DIR:tgfx2_opengl_navmesh_overlay>")
            endif()
            termin_set_rpath_test(tgfx2_opengl_navmesh_overlay)
            add_test(NAME tgfx2_opengl_navmesh_overlay
                COMMAND tgfx2_opengl_navmesh_overlay)
            set_tests_properties(tgfx2_opengl_navmesh_overlay PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
            termin_add_test_labels(tgfx2_opengl_navmesh_overlay
                "termin:capability:window"
                "termin:capability:opengl")
        endif()
    endif()

    # Vulkan smoke test (no GLFW needed — offscreen)
    if(TGFX2_ENABLE_VULKAN)
        add_executable(tgfx2_vulkan_stats_test
            tests/test_tgfx2_vulkan_stats.cpp)
        target_include_directories(tgfx2_vulkan_stats_test PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/tgfx2/vulkan)
        target_compile_definitions(tgfx2_vulkan_stats_test PRIVATE TGFX2_HAS_VULKAN)
        target_link_libraries(tgfx2_vulkan_stats_test PRIVATE termin_graphics2)
        termin_set_rpath_test(tgfx2_vulkan_stats_test)
        add_test(NAME tgfx2_vulkan_stats_disabled
            COMMAND tgfx2_vulkan_stats_test disabled)
        set_tests_properties(tgfx2_vulkan_stats_disabled PROPERTIES
            ENVIRONMENT "TGFX2_VULKAN_STATS=0")
        add_test(NAME tgfx2_vulkan_stats_enabled
            COMMAND tgfx2_vulkan_stats_test enabled)
        set_tests_properties(tgfx2_vulkan_stats_enabled PROPERTIES
            ENVIRONMENT "TGFX2_VULKAN_STATS=1")
        termin_add_test_labels(tgfx2_vulkan_stats_disabled
            "termin:capability:vulkan")
        termin_add_test_labels(tgfx2_vulkan_stats_enabled
            "termin:capability:vulkan")

        add_executable(tgfx2_per_pipeline_layout_test tests/test_per_pipeline_layout.cpp)
        target_compile_definitions(tgfx2_per_pipeline_layout_test PRIVATE TGFX2_HAS_VULKAN)
        target_link_libraries(tgfx2_per_pipeline_layout_test PRIVATE termin_graphics2 Vulkan::Vulkan)
        termin_set_rpath_test(tgfx2_per_pipeline_layout_test)
        add_test(NAME tgfx2_per_pipeline_layout_test COMMAND tgfx2_per_pipeline_layout_test)
        set_tests_properties(tgfx2_per_pipeline_layout_test PROPERTIES
            TIMEOUT 20
            SKIP_REGULAR_EXPRESSION "${TGFX2_VULKAN_RUNTIME_SKIP_RE}")
        termin_add_test_labels(tgfx2_per_pipeline_layout_test
            "termin:capability:vulkan")

        add_executable(tgfx2_vulkan_smoke tests/test_tgfx2_vulkan_smoke.cpp)
        target_compile_definitions(tgfx2_vulkan_smoke
            PRIVATE
                TGFX2_HAS_VULKAN
                TGFX2_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(tgfx2_vulkan_smoke PRIVATE termin_graphics2 Vulkan::Vulkan)
        termin_set_rpath_test(tgfx2_vulkan_smoke)
        add_test(NAME tgfx2_vulkan_smoke COMMAND tgfx2_vulkan_smoke)
        set_tests_properties(tgfx2_vulkan_smoke PROPERTIES
            TIMEOUT 20
            SKIP_REGULAR_EXPRESSION "${TGFX2_VULKAN_RUNTIME_SKIP_RE}")
        termin_add_test_labels(tgfx2_vulkan_smoke "termin:capability:vulkan")
    endif()

    if(TGFX2_ENABLE_VULKAN AND BUILD_TGFX2_WINDOW_TESTS)
        # On-screen Vulkan smoke test. SDL2 lookup: Ubuntu's SDL2
        # ships `sdl2-config.cmake` (lowercase, non-standard) so we
        # skip find_package and go straight to find_path/find_library.
        set(SDL2_SMOKE_FOUND FALSE)
        find_path(SDL2_SMOKE_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
        find_library(SDL2_SMOKE_LIBRARY SDL2)
        if(WIN32 AND SDL2_SMOKE_LIBRARY)
            get_filename_component(SDL2_SMOKE_LIBRARY_DIR
                "${SDL2_SMOKE_LIBRARY}" DIRECTORY)
            find_file(SDL2_SMOKE_DLL SDL2.dll
                PATHS
                    "${SDL2_SMOKE_LIBRARY_DIR}"
                    "${SDL2_SMOKE_LIBRARY_DIR}/../bin"
                NO_DEFAULT_PATH)
        endif()
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
            if(SDL2_SMOKE_DLL)
                add_custom_command(TARGET tgfx2_vulkan_window POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${SDL2_SMOKE_DLL}"
                        "$<TARGET_FILE_DIR:tgfx2_vulkan_window>")
            endif()
            termin_set_rpath_test(tgfx2_vulkan_window)
            add_test(NAME tgfx2_vulkan_window COMMAND tgfx2_vulkan_window)
            set_tests_properties(tgfx2_vulkan_window PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}")
            termin_add_test_labels(tgfx2_vulkan_window
                "termin:capability:window"
                "termin:capability:vulkan")
        endif()
    endif()

    if(TGFX2_ENABLE_D3D11)
        add_executable(tgfx2_d3d11_smoke tests/test_tgfx2_d3d11_smoke.cpp)
        target_compile_definitions(tgfx2_d3d11_smoke PRIVATE TGFX2_HAS_D3D11)
        target_link_libraries(tgfx2_d3d11_smoke PRIVATE termin_graphics2 d3dcompiler)
        termin_set_rpath_test(tgfx2_d3d11_smoke)
        add_test(NAME tgfx2_d3d11_smoke COMMAND tgfx2_d3d11_smoke)
        set_tests_properties(tgfx2_d3d11_smoke PROPERTIES
            TIMEOUT 10
            SKIP_REGULAR_EXPRESSION "D3D11CreateDevice failed|No D3D11")
        termin_add_test_labels(tgfx2_d3d11_smoke "termin:capability:d3d11")

        if(BUILD_TGFX2_D3D11_WINDOW_TEST)
            set(SDL2_SMOKE_FOUND FALSE)
            find_path(SDL2_SMOKE_INCLUDE_DIR SDL.h PATH_SUFFIXES SDL2)
            find_library(SDL2_SMOKE_LIBRARY SDL2)
            if(SDL2_SMOKE_INCLUDE_DIR AND SDL2_SMOKE_LIBRARY)
                set(SDL2_SMOKE_FOUND TRUE)
                message(STATUS "tgfx2 D3D11 window smoke: SDL2 at ${SDL2_SMOKE_LIBRARY}")
            else()
                message(STATUS "tgfx2 D3D11 window smoke: SDL2 not found; skipping")
            endif()
            if(SDL2_SMOKE_FOUND)
                add_executable(tgfx2_d3d11_window tests/test_tgfx2_d3d11_window.cpp)
                target_include_directories(tgfx2_d3d11_window PRIVATE ${SDL2_SMOKE_INCLUDE_DIR})
                target_compile_definitions(tgfx2_d3d11_window PRIVATE TGFX2_HAS_D3D11)
                target_link_libraries(tgfx2_d3d11_window
                    PRIVATE termin_graphics2 ${SDL2_SMOKE_LIBRARY})
                get_filename_component(SDL2_SMOKE_LIBRARY_DIR "${SDL2_SMOKE_LIBRARY}" DIRECTORY)
                find_file(SDL2_SMOKE_DLL
                    NAMES SDL2.dll
                    PATHS
                        "${SDL2_SMOKE_LIBRARY_DIR}"
                        "${SDL2_SMOKE_LIBRARY_DIR}/../bin"
                        "${SDL2_SMOKE_LIBRARY_DIR}/..")
                if(SDL2_SMOKE_DLL)
                    add_custom_command(TARGET tgfx2_d3d11_window POST_BUILD
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${SDL2_SMOKE_DLL}"
                            "$<TARGET_FILE_DIR:tgfx2_d3d11_window>")
                endif()
                termin_set_rpath_test(tgfx2_d3d11_window)
                add_test(NAME tgfx2_d3d11_window COMMAND tgfx2_d3d11_window)
            set_tests_properties(tgfx2_d3d11_window PROPERTIES
                TIMEOUT 10
                SKIP_REGULAR_EXPRESSION "${TGFX2_WINDOW_SKIP_RE}|D3D11CreateDevice failed")
            termin_add_test_labels(tgfx2_d3d11_window
                "termin:capability:window"
                "termin:capability:d3d11")
            endif()
        endif()
    endif()
endif()

# --- Install ---
include(GNUInstallDirs)

set(TERMIN_GRAPHICS_INSTALL_TARGETS
    termin_graphics
    termin_graphics_binding_policy
    termin_graphics2
    glad)

install(TARGETS ${TERMIN_GRAPHICS_INSTALL_TARGETS}
    EXPORT termin_graphicsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/tgfx include/tgfx2
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY
    ../termin-thirdparty/glad/include/glad
    ../termin-thirdparty/glad/include/KHR
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

termin_label_tests_in_directory("termin-graphics")

install(DIRECTORY resources/builtin_shaders
    DESTINATION ${CMAKE_INSTALL_DATADIR}/termin
)

install(DIRECTORY "${CMAKE_BINARY_DIR}/share/termin/shaders"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/termin
    OPTIONAL
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
if(TGFX2_ENABLE_D3D11)
    set(PACKAGE_TGFX2_ENABLE_D3D11 TRUE)
else()
    set(PACKAGE_TGFX2_ENABLE_D3D11 FALSE)
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
