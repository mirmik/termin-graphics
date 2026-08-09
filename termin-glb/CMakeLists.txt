cmake_minimum_required(VERSION 3.16)
project(termin_glb VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings (_glb_native)" OFF)

if(COMMAND termin_require_package)
    termin_require_package(termin_mesh tmesh::termin_mesh)
else()
    find_package(termin_mesh REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_glb SHARED
    src/cgltf_impl.cpp
    src/native_backend.cpp
)
add_library(termin_glb::termin_glb ALIAS termin_glb)

target_compile_definitions(termin_glb PRIVATE TERMIN_GLB_EXPORTS)
target_include_directories(termin_glb
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/cgltf
)
target_link_libraries(termin_glb PUBLIC tmesh::termin_mesh)

set_target_properties(termin_glb PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(termin_glb)

if(MSVC)
    target_compile_options(termin_glb PRIVATE /W3)
else()
    target_compile_options(termin_glb PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.14 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_glb_native NB_SHARED
        python/bindings/glb_native_module.cpp
    )
    target_link_libraries(_glb_native PRIVATE termin_glb)
    termin_set_rpath_python(_glb_native)
endif()

if(TERMIN_BUILD_TESTS)
    add_executable(termin_glb_native_backend_test
        tests/test_native_backend.cpp
    )
    target_link_libraries(termin_glb_native_backend_test PRIVATE termin_glb)
    termin_set_rpath_test(termin_glb_native_backend_test)
    add_test(NAME termin_glb_native_backend_test COMMAND termin_glb_native_backend_test)
    if(COMMAND termin_label_tests_in_directory)
        termin_label_tests_in_directory("termin-glb")
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_glb
    EXPORT termin_glbTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TERMIN_BUILD_PYTHON)
    install(TARGETS _glb_native DESTINATION lib/python/termin/glb)
endif()

install(DIRECTORY include/termin
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT termin_glbTargets
    FILE termin_glbTargets.cmake
    NAMESPACE termin_glb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_glb
)

configure_package_config_file(
    cmake/termin_glbConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_glbConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_glb
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_glbConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_glbConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_glbConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_glb
)

