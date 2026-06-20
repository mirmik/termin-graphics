cmake_minimum_required(VERSION 3.14)
project(termin_materials VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TERMIN_MATERIALS_BUILD_SHARED "Build shared library" ON)
option(TERMIN_BUILD_PYTHON "Build Python bindings for termin-materials" OFF)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(termin_graphics tgfx::termin_graphics tgfx::termin_graphics2)
else()
    find_package(termin_graphics REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

set(TERMIN_MATERIALS_SOURCES
    src/glsl_preprocessor.cpp
    src/shader_parser.cpp
)

set(TERMIN_MATERIALS_HEADERS
    include/termin/materials/glsl_preprocessor.hpp
    include/termin/materials/shader_parser.hpp
)

if(TERMIN_MATERIALS_BUILD_SHARED)
    add_library(termin_materials SHARED ${TERMIN_MATERIALS_SOURCES} ${TERMIN_MATERIALS_HEADERS})
    target_compile_definitions(termin_materials PRIVATE TERMIN_MATERIALS_EXPORTS TC_EXPORTS)
else()
    add_library(termin_materials STATIC ${TERMIN_MATERIALS_SOURCES} ${TERMIN_MATERIALS_HEADERS})
endif()

add_library(termin_materials::termin_materials ALIAS termin_materials)

if(COMMAND termin_enable_pch_for)
    termin_enable_pch_for(termin_materials)
endif()

set_target_properties(termin_materials PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
termin_set_rpath_lib(termin_materials)

target_include_directories(termin_materials
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(termin_materials PUBLIC cxx_std_20)

target_link_libraries(termin_materials
    PUBLIC
        tcbase::termin_base
        tgfx::termin_graphics
        tgfx::termin_graphics2
)

if(MSVC)
    target_compile_options(termin_materials PRIVATE /W3 /wd4251 /wd4275)
    target_compile_definitions(termin_materials PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(termin_materials PRIVATE -Wall -Wextra)
endif()

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_materials_native NB_SHARED
        python/bindings/material_bindings.cpp
        python/bindings/materials_bindings.cpp
    )
    target_link_libraries(_materials_native PRIVATE termin_materials)
    target_include_directories(_materials_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    termin_set_rpath_python(_materials_native)
    install(TARGETS _materials_native DESTINATION lib/python/termin/materials)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python/termin/materials/
        DESTINATION lib/python/termin/materials
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
        PATTERN "*.so" EXCLUDE
        PATTERN "lib" EXCLUDE
    )
endif()

install(TARGETS termin_materials
    EXPORT termin_materialsTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

install(EXPORT termin_materialsTargets
    FILE termin_materialsTargets.cmake
    NAMESPACE termin_materials::
    DESTINATION lib/cmake/termin_materials
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/termin_materialsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_materialsConfig.cmake
    INSTALL_DESTINATION lib/cmake/termin_materials
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_materialsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_materialsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_materialsConfigVersion.cmake
    DESTINATION lib/cmake/termin_materials
)
