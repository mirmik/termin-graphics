cmake_minimum_required(VERSION 3.16)
project(termin_image VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(TERMIN_BUILD_PYTHON "Build Python bindings" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
include(TerminRpath)
include(TerminImageCodecs)
termin_configure_image_codecs()

add_library(termin_image SHARED
    src/image_decode.cpp
)
add_library(termin_image::termin_image ALIAS termin_image)

target_compile_definitions(termin_image PRIVATE TERMIN_IMAGE_EXPORTS)
target_include_directories(termin_image PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(termin_image PRIVATE
    ${TERMIN_IMAGE_CODEC_TARGETS}
)
termin_set_rpath_lib(termin_image)

if(MSVC)
    target_compile_options(termin_image PRIVATE /W3)
else()
    target_compile_options(termin_image PRIVATE -Wall -Wextra)
endif()

set_target_properties(termin_image PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if(TERMIN_BUILD_PYTHON)
    add_subdirectory(python)
endif()

include(GNUInstallDirs)

install(TARGETS termin_image
    EXPORT termin_imageTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/termin
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT termin_imageTargets
    FILE termin_imageTargets.cmake
    NAMESPACE termin_image::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_image
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/termin_imageConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/termin_imageConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_image
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/termin_imageConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/termin_imageConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/termin_imageConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/termin_image
)
