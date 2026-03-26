cmake_minimum_required(VERSION 3.16)
project(termin_graphics VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Find OpenGL
find_package(OpenGL REQUIRED)

# termin-base (provides tc_log shared logging)
find_package(termin_base REQUIRED)
find_package(termin_mesh REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

# --- GLAD (static library) ---
add_library(glad STATIC
    third/glad/src/glad.c
)
target_include_directories(glad PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/third/glad/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

# --- termin_graphics ---
set(TGFX_C_SOURCES
    src/tc_gpu.c
    src/tgfx_gpu_ops.c
    src/tc_gpu_context.c
    src/tc_gpu_share_group.c
    src/resources/tc_shader_registry.c
    src/resources/tc_texture_registry.c
    src/resources/tc_material_registry.c
    src/tgfx_resource_gpu.c
)

set(TGFX_CXX_SOURCES
    src/opengl/opengl_backend.cpp
    src/opengl/opengl_backend_singleton.cpp
    src/opengl/opengl_framebuffer.cpp
    src/tgfx_texture_handle.cpp
)

add_library(termin_graphics SHARED ${TGFX_C_SOURCES} ${TGFX_CXX_SOURCES})
target_compile_definitions(termin_graphics PRIVATE TGFX_EXPORTS TC_EXPORTS)

if(MSVC)
    target_compile_options(termin_graphics PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(termin_graphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_graphics PUBLIC
    glad
    OpenGL::GL
    tcbase::termin_base
    tmesh::termin_mesh
)

# Set library version
set_target_properties(termin_graphics PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(termin_graphics)

# --- termin_graphics2 (new backend-neutral API) ---
set(TGFX2_SOURCES
    src/tgfx2/device_factory.cpp
    src/tgfx2/opengl/opengl_render_device.cpp
    src/tgfx2/opengl/opengl_command_list.cpp
    src/tgfx2/opengl/opengl_type_conversions.cpp
    src/tgfx2_gpu_ops.cpp
)

# --- Optional Vulkan backend ---
option(TGFX2_ENABLE_VULKAN "Enable Vulkan backend for tgfx2" OFF)

if(TGFX2_ENABLE_VULKAN)
    find_package(Vulkan REQUIRED)

    # VMA (Vulkan Memory Allocator) via FetchContent
    include(FetchContent)
    FetchContent_Declare(vma
        GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
        GIT_TAG v3.1.0
    )
    set(VMA_STATIC_VULKAN_FUNCTIONS OFF CACHE BOOL "" FORCE)
    set(VMA_DYNAMIC_VULKAN_FUNCTIONS ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(vma)

    # shaderc: prefer shared library, fall back to combined static
    find_library(SHADERC_LIB NAMES shaderc shaderc_combined
        PATHS "$ENV{VULKAN_SDK}/Lib" "${Vulkan_INCLUDE_DIR}/../Lib"
              "C:/VulkanSDK/1.4.341.1/Lib"
    )

    list(APPEND TGFX2_SOURCES
        src/tgfx2/vulkan/vulkan_render_device.cpp
        src/tgfx2/vulkan/vulkan_command_list.cpp
        src/tgfx2/vulkan/vulkan_type_conversions.cpp
        src/tgfx2/vulkan/vulkan_shader_compiler.cpp
        src/tgfx2/vulkan/vulkan_swapchain.cpp
    )
endif()

add_library(termin_graphics2 SHARED ${TGFX2_SOURCES})
target_compile_definitions(termin_graphics2 PRIVATE TGFX2_EXPORTS)

if(TGFX2_ENABLE_VULKAN)
    target_compile_definitions(termin_graphics2 PUBLIC TGFX2_HAS_VULKAN)
    target_link_libraries(termin_graphics2 PUBLIC Vulkan::Vulkan)
    target_link_libraries(termin_graphics2 PRIVATE ${SHADERC_LIB})
    # VMA is header-only; link PRIVATE to avoid export issues
    target_include_directories(termin_graphics2 PRIVATE ${vma_SOURCE_DIR}/include)
endif()

if(MSVC)
    target_compile_options(termin_graphics2 PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(termin_graphics2 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_graphics2 PUBLIC
    glad
    OpenGL::GL
    termin_graphics
)

set_target_properties(termin_graphics2 PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(termin_graphics2)

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
    )
    target_link_libraries(tgfx_tests PRIVATE termin_graphics)

    add_test(NAME tgfx_tests COMMAND tgfx_tests)

    add_executable(tgfx_mesh_test tests/test_mesh.c)
    target_link_libraries(tgfx_mesh_test PRIVATE termin_graphics)
    add_test(NAME tgfx_mesh_test COMMAND tgfx_mesh_test)

    add_custom_target(run_tests
        COMMAND tgfx_tests
        COMMAND tgfx_mesh_test
        DEPENDS tgfx_tests tgfx_mesh_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# --- tgfx2 smoke test (uses GLFW via FetchContent) ---
option(BUILD_TGFX2_TESTS "Build tgfx2 smoke tests" OFF)

if(BUILD_TGFX2_TESTS)
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

    enable_testing()

    add_executable(tgfx2_smoke tests/test_tgfx2_smoke.cpp)
    target_link_libraries(tgfx2_smoke PRIVATE termin_graphics2 glfw)
    add_test(NAME tgfx2_smoke COMMAND tgfx2_smoke)

    # tgfx2 interop test (tgfx2-backed gpu_ops)
    add_executable(tgfx2_interop_test tests/test_tgfx2_interop.cpp)
    target_link_libraries(tgfx2_interop_test PRIVATE termin_graphics2 glfw)
    add_test(NAME tgfx2_interop_test COMMAND tgfx2_interop_test)

    # Vulkan smoke test (no GLFW needed — offscreen)
    if(TGFX2_ENABLE_VULKAN)
        add_executable(tgfx2_vulkan_smoke tests/test_tgfx2_vulkan_smoke.cpp)
        target_link_libraries(tgfx2_vulkan_smoke PRIVATE termin_graphics2)
        add_test(NAME tgfx2_vulkan_smoke COMMAND tgfx2_vulkan_smoke)
    endif()
endif()

# --- Install ---
include(GNUInstallDirs)

install(TARGETS termin_graphics termin_graphics2 glad
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
