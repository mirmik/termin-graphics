cmake_minimum_required(VERSION 3.16)
project(termin_nodegraph VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings" OFF)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_nodegraph_core SHARED
    src/c_api.cpp
    src/graph.cpp
    src/serialization.cpp
)
add_library(termin_nodegraph::core ALIAS termin_nodegraph_core)
set_target_properties(termin_nodegraph_core PROPERTIES EXPORT_NAME core)
target_compile_definitions(termin_nodegraph_core PRIVATE
    TERMIN_NODEGRAPH_CORE_EXPORTS)
target_include_directories(termin_nodegraph_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_nodegraph_core PUBLIC
    tcbase::termin_base
)
termin_set_rpath_lib(termin_nodegraph_core)

if(TERMIN_BUILD_PYTHON)
    find_package(Python 3.14 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    nanobind_add_module(_nodegraph_native NB_SHARED
        python/bindings/nodegraph_module.cpp)
    target_link_libraries(_nodegraph_native PRIVATE
        termin_nodegraph_core tcbase::termin_base)
    termin_set_rpath_python(_nodegraph_native)
    install(TARGETS _nodegraph_native DESTINATION lib/python/tcnodegraph)
endif()

if(TERMIN_BUILD_TESTS)
    enable_testing()
    add_executable(termin_nodegraph_core_test
        tests/test_native_graph.cpp)
    target_link_libraries(termin_nodegraph_core_test PRIVATE
        termin_nodegraph_core)
    termin_set_rpath_test(termin_nodegraph_core_test)
    add_test(NAME termin_nodegraph_core_test
        COMMAND termin_nodegraph_core_test)

    add_executable(termin_nodegraph_c_api_test
        tests/test_c_api.c)
    target_link_libraries(termin_nodegraph_c_api_test PRIVATE
        termin_nodegraph_core)
    termin_set_rpath_test(termin_nodegraph_c_api_test)
    add_test(NAME termin_nodegraph_c_api_test
        COMMAND termin_nodegraph_c_api_test)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_nodegraph_core
    EXPORT termin_nodegraphTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT termin_nodegraphTargets
    FILE termin_nodegraphTargets.cmake
    NAMESPACE termin_nodegraph::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_nodegraph
)
configure_package_config_file(
    cmake/termin_nodegraphConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_nodegraphConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_nodegraph
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_nodegraphConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_nodegraphConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_nodegraphConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_nodegraph
)

if(COMMAND termin_label_tests_in_directory)
    termin_label_tests_in_directory("termin-nodegraph")
endif()
