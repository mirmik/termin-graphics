cmake_minimum_required(VERSION 3.16)
project(termin_skeleton VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings (_skeleton_native)" OFF)
option(TERMIN_SKELETON_BUILD_TESTS "Build termin-skeleton tests" ON)

if(COMMAND termin_require_package)
    termin_require_package(termin_base tcbase::termin_base)
else()
    find_package(termin_base REQUIRED)
endif()
if(COMMAND termin_require_package)
    termin_require_package(termin_scene termin_scene::termin_scene)
else()
    find_package(termin_scene REQUIRED)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)

add_library(termin_skeleton SHARED
    src/termin_skeleton_version.cpp
    src/skeleton_data.cpp
    src/skeleton_instance.cpp
    src/tc_skeleton_registry.c
)
add_library(termin_skeleton::termin_skeleton ALIAS termin_skeleton)

target_compile_definitions(termin_skeleton PRIVATE TERMIN_SKELETON_EXPORTS TC_EXPORTS)

if(MSVC)
    target_compile_options(termin_skeleton PRIVATE /wd4251 /wd4275)
endif()

if(TERMIN_SKELETON_BUILD_TESTS)
    enable_testing()

    add_executable(termin_skeleton_data_test tests/test_skeleton_data.cpp)
    target_include_directories(termin_skeleton_data_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../termin-thirdparty/guard
    )
    target_link_libraries(termin_skeleton_data_test PRIVATE termin_skeleton)
    add_test(NAME termin_skeleton_data_test COMMAND termin_skeleton_data_test)
endif()

target_include_directories(termin_skeleton PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(termin_skeleton PUBLIC
    tcbase::termin_base
    termin_scene::termin_scene
)

set_target_properties(termin_skeleton PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
termin_set_rpath_lib(termin_skeleton)

if(TERMIN_BUILD_PYTHON)
    if(NOT Python_EXECUTABLE)
        if(WIN32)
            set(Python_EXECUTABLE "python")
        else()
            set(Python_EXECUTABLE "python3")
        endif()
    endif()

    find_package(Python 3.14 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)
    if(COMMAND termin_require_package)
    termin_require_package(termin_inspect termin_inspect::termin_inspect)
else()
    find_package(termin_inspect REQUIRED)
endif()

    nanobind_add_module(_skeleton_native NB_SHARED
        cpp/bindings/skeleton_module.cpp
    )
    target_include_directories(_skeleton_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(_skeleton_native PRIVATE
        termin_skeleton
        termin_inspect::termin_inspect
        termin_inspect::termin_inspect_python
    )
    termin_set_rpath_python(_skeleton_native)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_skeleton
    EXPORT termin_skeletonTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(TERMIN_BUILD_PYTHON)
    if(TERMIN_SKELETON_PYTHON_PREFIX)
        install(TARGETS _skeleton_native DESTINATION ${TERMIN_SKELETON_PYTHON_PREFIX}/skeleton)
    else()
        install(TARGETS _skeleton_native DESTINATION lib/python/termin/skeleton)
    endif()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/python/termin/skeleton/
        DESTINATION lib/python/termin/skeleton
        PATTERN "__pycache__" EXCLUDE
        PATTERN "*.pyc" EXCLUDE
    )
endif()

install(DIRECTORY include/termin
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY include/resources
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT termin_skeletonTargets
    FILE termin_skeletonTargets.cmake
    NAMESPACE termin_skeleton::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_skeleton
)

configure_package_config_file(
    cmake/termin_skeletonConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_skeletonConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_skeleton
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_skeletonConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_skeletonConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_skeletonConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_skeleton
)

termin_label_tests_in_directory("termin-skeleton")
