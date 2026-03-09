cmake_minimum_required(VERSION 3.16)
project(termin_mesh VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(TERMIN_BUILD_PYTHON "Build Python bindings" OFF)

find_package(termin_base REQUIRED)

set(TERMIN_MESH_C_SOURCES
    src/tgfx_types.c
    src/resources/tc_mesh.c
    src/resources/tc_mesh_registry.c
)

set(TERMIN_MESH_CXX_SOURCES
    src/tgfx_mesh_handle.cpp
)

add_library(termin_mesh SHARED ${TERMIN_MESH_C_SOURCES} ${TERMIN_MESH_CXX_SOURCES})
add_library(tmesh::termin_mesh ALIAS termin_mesh)

target_compile_definitions(termin_mesh PRIVATE TGFX_EXPORTS)

target_include_directories(termin_mesh PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_mesh PUBLIC
    tcbase::termin_base
)

set_target_properties(termin_mesh PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
if(NOT WIN32)
    set_target_properties(termin_mesh PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if(TERMIN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

include(GNUInstallDirs)

install(TARGETS termin_mesh
    EXPORT termin_meshTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/tgfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT termin_meshTargets
    FILE termin_meshTargets.cmake
    NAMESPACE tmesh::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_mesh
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/termin_meshConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_meshConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_mesh
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_meshConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_meshConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_meshConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_mesh
)
