cmake_minimum_required(VERSION 3.16)
project(tcplot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings" OFF)

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
if(COMMAND termin_require_package)
    termin_require_package(termin_graphics tgfx::termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_graphics REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(
        termin_visual_scene
        termin_visual_scene::termin_visual_scene)
else()
    find_package(termin_visual_scene REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(
        termin_render_core
        termin_render_core::termin_render_core)
else()
    find_package(termin_render_core REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

# --- tcplot core library ---
#
# Pure C++ port of the Python tcplot package (tcplot/*.py). Offers
# host-agnostic plot engines (PlotEngine2D for 2D axes + series,
# PlotEngine3D for 3D scenes with billboard labels) backed by tgfx2.
#
# The library is split into orthogonal translation units:
#   - styles / axes     : color palette, tick/label helpers (no deps).
#   - orbit_camera      : compatibility wrapper over termin::OrbitCamera.
#   - plot_data         : series + PlotData container.
#   - engine3d          : 3D plot state + rendering through tgfx2.
#   - engine2d          : 2D plot state + rendering through tgfx2.
set(TCPLOT_SOURCES
    src/styles.cpp
    src/axes.cpp
    src/orbit_camera.cpp
    src/plot_data.cpp
    src/plot_frame2d.cpp
    src/plot_projection2d.cpp
    src/plot_grid_item2d.cpp
    src/plot_series_item2d.cpp
    src/plot_layout2d.cpp
    src/chart_interaction2d.cpp
    src/plot_annotations2d.cpp
    src/retained_chart2d.cpp
    src/retained_scene_renderer2d.cpp
    src/retained_chart3d.cpp
    src/engine3d.cpp
    src/engine2d.cpp
    src/plot_view3d.cpp
    src/plot_view2d.cpp
    src/plot_view2d_multi.cpp
    src/gpu_host.cpp
)

add_library(tcplot SHARED ${TCPLOT_SOURCES})
add_library(tcplot::tcplot ALIAS tcplot)

if(COMMAND termin_enable_pch_for)
    termin_enable_pch_for(tcplot)
endif()

target_compile_definitions(tcplot PRIVATE TCPLOT_EXPORTS)

if(MSVC)
    # Suppress STL-in-dllexport noise: harmless with same CRT across libs.
    target_compile_options(tcplot PRIVATE /wd4251 /wd4275)
endif()

target_include_directories(tcplot PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(tcplot PUBLIC
    tcbase::termin_base
    tmesh::termin_mesh
    tgfx::termin_graphics2
    termin_visual_scene::termin_visual_scene
    termin_render_core::termin_render_core
)

set_target_properties(tcplot PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(tcplot)

# --- Python bindings (optional, Phase D) ---
if(TERMIN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

if(TERMIN_BUILD_TESTS)
    enable_testing()
    add_executable(tcplot_plot_frame2d_test
        tests/test_plot_frame2d.cpp)
    target_link_libraries(tcplot_plot_frame2d_test PRIVATE
        tcplot)
    termin_set_rpath_test(tcplot_plot_frame2d_test)
    add_test(NAME tcplot_plot_frame2d_test
        COMMAND tcplot_plot_frame2d_test)

    add_executable(tcplot_plot_projection2d_test
        tests/test_plot_projection2d.cpp)
    target_link_libraries(tcplot_plot_projection2d_test PRIVATE
        tcplot)
    termin_set_rpath_test(tcplot_plot_projection2d_test)
    add_test(NAME tcplot_plot_projection2d_test
        COMMAND tcplot_plot_projection2d_test)

    add_executable(tcplot_plot_grid_item2d_test
        tests/test_plot_grid_item2d.cpp)
    target_link_libraries(tcplot_plot_grid_item2d_test PRIVATE
        tcplot)
    termin_set_rpath_test(tcplot_plot_grid_item2d_test)
    add_test(NAME tcplot_plot_grid_item2d_test
        COMMAND tcplot_plot_grid_item2d_test)

    add_executable(tcplot_plot_series_item2d_test
        tests/test_plot_series_item2d.cpp)
    target_link_libraries(tcplot_plot_series_item2d_test PRIVATE
        tcplot)
    termin_set_rpath_test(tcplot_plot_series_item2d_test)
    add_test(NAME tcplot_plot_series_item2d_test
        COMMAND tcplot_plot_series_item2d_test)

    add_executable(tcplot_plot_layout2d_test
        tests/test_plot_layout2d.cpp)
    target_link_libraries(tcplot_plot_layout2d_test PRIVATE
        tcplot)
    target_compile_definitions(tcplot_plot_layout2d_test PRIVATE
        TCPLOT_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf"
        TCPLOT_TEST_FONT_ALT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Contrib/imgui/misc/fonts/Roboto-Medium.ttf")
    termin_set_rpath_test(tcplot_plot_layout2d_test)
    add_test(NAME tcplot_plot_layout2d_test
        COMMAND tcplot_plot_layout2d_test)

    add_executable(tcplot_plot_annotations2d_test
        tests/test_plot_annotations2d.cpp)
    target_link_libraries(tcplot_plot_annotations2d_test PRIVATE
        tcplot)
    termin_set_rpath_test(tcplot_plot_annotations2d_test)
    add_test(NAME tcplot_plot_annotations2d_test
        COMMAND tcplot_plot_annotations2d_test)

    if(TARGET termin_shaderc)
        add_executable(tcplot_plot_series_performance_test
            tests/test_plot_series_performance.cpp)
        target_link_libraries(tcplot_plot_series_performance_test PRIVATE
            tcplot)
        target_compile_definitions(tcplot_plot_series_performance_test PRIVATE
            TCPLOT_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf")
        add_dependencies(tcplot_plot_series_performance_test termin_shaderc)
        termin_set_rpath_test(tcplot_plot_series_performance_test)
        add_test(NAME tcplot_plot_series_performance_test
            COMMAND tcplot_plot_series_performance_test)
        set_tests_properties(tcplot_plot_series_performance_test PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 30
            ENVIRONMENT
                "TERMIN_SHADERC=$<TARGET_FILE:termin_shaderc>;TERMIN_BUILTIN_SHADER_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../termin-graphics/resources/builtin_shaders")
        termin_add_test_labels(
            tcplot_plot_series_performance_test
            "termin:capability:vulkan")

        add_executable(tcplot_plot_marker_offscreen_test
            tests/test_plot_marker_offscreen.cpp)
        target_link_libraries(tcplot_plot_marker_offscreen_test PRIVATE
            tcplot)
        target_compile_definitions(tcplot_plot_marker_offscreen_test PRIVATE
            TCPLOT_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf")
        add_dependencies(tcplot_plot_marker_offscreen_test termin_shaderc)
        termin_set_rpath_test(tcplot_plot_marker_offscreen_test)
        add_test(NAME tcplot_plot_marker_offscreen_test
            COMMAND tcplot_plot_marker_offscreen_test)
        set_tests_properties(tcplot_plot_marker_offscreen_test PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 20
            ENVIRONMENT
                "TERMIN_SHADERC=$<TARGET_FILE:termin_shaderc>;TERMIN_BUILTIN_SHADER_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../termin-graphics/resources/builtin_shaders")
        termin_add_test_labels(
            tcplot_plot_marker_offscreen_test
            "termin:capability:vulkan")

        add_executable(tcplot_retained_chart3d_test
            tests/test_retained_chart3d.cpp)
        target_link_libraries(tcplot_retained_chart3d_test PRIVATE
            tcplot)
        target_compile_definitions(tcplot_retained_chart3d_test PRIVATE
            TCPLOT_TEST_FONT="${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/recastnavigation/RecastDemo/Bin/DroidSans.ttf")
        add_dependencies(tcplot_retained_chart3d_test termin_shaderc)
        termin_set_rpath_test(tcplot_retained_chart3d_test)
        add_test(NAME tcplot_retained_chart3d_test
            COMMAND tcplot_retained_chart3d_test)
        set_tests_properties(tcplot_retained_chart3d_test PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 30
            ENVIRONMENT
                "TERMIN_SHADERC=$<TARGET_FILE:termin_shaderc>;TERMIN_BUILTIN_SHADER_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/../termin-graphics/resources/builtin_shaders")
        termin_add_test_labels(
            tcplot_retained_chart3d_test
            "termin:capability:vulkan")
    endif()
endif()

# --- Install ---
include(GNUInstallDirs)

install(TARGETS tcplot
    EXPORT tcplotTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/tcplot
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT tcplotTargets
    FILE tcplotTargets.cmake
    NAMESPACE tcplot::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/tcplotConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/tcplotConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/tcplotConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/tcplotConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/tcplotConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/tcplot
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("tcplot")
endif()
