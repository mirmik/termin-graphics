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
    src/tgfx_gpu_ops.c
    src/tgfx_types.c
    src/tgfx_intern_string.c
    src/tc_pool.c
    src/tc_resource_map.c
    src/tc_gpu_context.c
    src/tc_gpu_share_group.c
    src/resources/tc_mesh.c
    src/resources/tc_shader_registry.c
    src/resources/tc_texture_registry.c
    src/resources/tc_mesh_registry.c
    src/resources/tc_material_registry.c
    src/tgfx_resource_gpu.c
)

set(TGFX_CXX_SOURCES
    src/opengl/opengl_backend.cpp
    src/opengl/opengl_backend_singleton.cpp
    src/opengl/opengl_framebuffer.cpp
    src/tgfx_texture_handle.cpp
    src/tgfx_mesh_handle.cpp
)

add_library(termin_graphics SHARED ${TGFX_C_SOURCES} ${TGFX_CXX_SOURCES})
target_compile_definitions(termin_graphics PRIVATE TGFX_EXPORTS)

target_include_directories(termin_graphics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_graphics PUBLIC
    glad
    OpenGL::GL
    tcbase::termin_base
)

# Set library version
set_target_properties(termin_graphics PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
if(NOT WIN32)
    set_target_properties(termin_graphics PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

# --- Python bindings ---
option(TGFX_BUILD_PYTHON "Build Python bindings" OFF)

if(TGFX_BUILD_PYTHON)
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

    add_custom_target(run_tests
        COMMAND tgfx_tests
        DEPENDS tgfx_tests
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endif()

# --- Install ---
include(GNUInstallDirs)

install(TARGETS termin_graphics glad
    EXPORT termin_graphicsTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/tgfx
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
