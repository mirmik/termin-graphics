cmake_minimum_required(VERSION 3.16)
project(termin_animation VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings (_animation_native)" OFF)

find_package(termin_base REQUIRED)
find_package(termin_scene REQUIRED)
find_package(termin_inspect REQUIRED)
find_package(termin_skeleton REQUIRED)

find_library(TERMIN_CORE_LIBRARY
    NAMES termin_core
    PATHS ${CMAKE_PREFIX_PATH} /opt/termin
    PATH_SUFFIXES lib
    REQUIRED
)

add_library(termin_animation SHARED
    src/termin_animation_version.cpp
)
add_library(termin_animation::termin_animation ALIAS termin_animation)

target_compile_definitions(termin_animation PRIVATE TERMIN_ANIMATION_EXPORTS)

target_include_directories(termin_animation PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../termin/core_c/include>
    $<INSTALL_INTERFACE:include>
    $<INSTALL_INTERFACE:include/core_c>
)

target_link_libraries(termin_animation PUBLIC
    ${TERMIN_CORE_LIBRARY}
    tcbase::termin_base
    termin_scene::termin_scene
    termin_inspect::termin_inspect
    termin_skeleton::termin_skeleton
)

set_target_properties(termin_animation PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
)
if(NOT WIN32)
    set_target_properties(termin_animation PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()

if(TERMIN_BUILD_PYTHON)
    if(NOT Python_EXECUTABLE)
        if(WIN32)
            set(Python_EXECUTABLE "python")
        else()
            set(Python_EXECUTABLE "python3")
        endif()
    endif()

    find_package(Python 3.8 COMPONENTS Interpreter Development REQUIRED)
    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(_animation_native NB_SHARED
        ${CMAKE_CURRENT_LIST_DIR}/../termin/cpp/termin/bindings/animation/animation_module.cpp
    )
    target_include_directories(_animation_native PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_LIST_DIR}/../termin/cpp
    )
    target_link_libraries(_animation_native PRIVATE
        termin_animation
        termin_inspect::termin_inspect_python
    )
    set_target_properties(_animation_native PROPERTIES
        INSTALL_RPATH "$ORIGIN;$ORIGIN/..;$ORIGIN/../..;$ORIGIN/../../..;/opt/termin/lib"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS termin_animation
    EXPORT termin_animationTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(TERMIN_BUILD_PYTHON)
    if(TERMIN_ANIMATION_PYTHON_PREFIX)
        install(TARGETS _animation_native DESTINATION ${TERMIN_ANIMATION_PYTHON_PREFIX}/visualization/animation)
        install(FILES
            python/termin/visualization/animation/__init__.py
            python/termin/visualization/animation/channel.py
            python/termin/visualization/animation/clip.py
            python/termin/visualization/animation/clip_io.py
            DESTINATION ${TERMIN_ANIMATION_PYTHON_PREFIX}/visualization/animation
        )
    else()
        install(TARGETS _animation_native DESTINATION lib/python/termin/visualization/animation)
        install(FILES
            python/termin/visualization/animation/__init__.py
            python/termin/visualization/animation/channel.py
            python/termin/visualization/animation/clip.py
            python/termin/visualization/animation/clip_io.py
            DESTINATION lib/python/termin/visualization/animation
        )
    endif()
endif()

install(DIRECTORY include/termin
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/../termin/core_c/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/core_c
)

install(EXPORT termin_animationTargets
    FILE termin_animationTargets.cmake
    NAMESPACE termin_animation::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_animation
)

configure_package_config_file(
    cmake/termin_animationConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_animationConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_animation
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_animationConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_animationConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_animationConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_animation
)
