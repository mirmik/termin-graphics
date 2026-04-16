cmake_minimum_required(VERSION 3.16)
project(tcplot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TCPLOT_BUILD_PYTHON "Build Python bindings" OFF)

find_package(termin_base REQUIRED)
find_package(termin_mesh REQUIRED)
find_package(termin_graphics REQUIRED)

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
#   - orbit_camera      : 3D camera math on termin::Mat44 / Vec3.
#   - plot_data         : series + PlotData container.
#   - engine3d          : 3D plot state + rendering through tgfx2.
#   - engine2d          : 2D plot state + rendering through tgfx2.
set(TCPLOT_SOURCES
    src/styles.cpp
    src/axes.cpp
    src/orbit_camera.cpp
    src/plot_data.cpp
    src/engine3d.cpp
    src/engine2d.cpp
)

add_library(tcplot SHARED ${TCPLOT_SOURCES})
add_library(tcplot::tcplot ALIAS tcplot)

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
)

set_target_properties(tcplot PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(tcplot)

# --- Python bindings (optional, Phase D) ---
if(TCPLOT_BUILD_PYTHON)
    add_subdirectory(python)
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
