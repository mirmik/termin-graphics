include_guard(GLOBAL)

set(
    TERMIN_ZLIB_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/zlib"
    CACHE INTERNAL "Termin bundled zlib source directory"
)
set(
    TERMIN_LIBPNG_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/libpng"
    CACHE INTERNAL "Termin bundled libpng source directory"
)
set(
    TERMIN_LIBJPEG_TURBO_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/libjpeg-turbo"
    CACHE INTERNAL "Termin bundled libjpeg-turbo source directory"
)
set(
    TERMIN_LIBWEBP_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/libwebp"
    CACHE INTERNAL "Termin bundled libwebp source directory"
)

if(WIN32)
    set(_TERMIN_USE_BUNDLED_IMAGE_CODECS_DEFAULT ON)
else()
    set(_TERMIN_USE_BUNDLED_IMAGE_CODECS_DEFAULT OFF)
endif()

option(
    TERMIN_USE_BUNDLED_IMAGE_CODECS
    "Prefer zlib/libpng/libjpeg-turbo/libwebp source submodules from termin-thirdparty for termin-image"
    ${_TERMIN_USE_BUNDLED_IMAGE_CODECS_DEFAULT}
)

function(_termin_require_image_codec_source name source_dir marker)
    if(NOT EXISTS "${source_dir}/${marker}")
        message(FATAL_ERROR
            "${name} bundled source is missing at ${source_dir}. "
            "Run `git submodule update --init --recursive -- ${source_dir}` "
            "or configure with -DTERMIN_USE_BUNDLED_IMAGE_CODECS=OFF."
        )
    endif()
endfunction()

function(_termin_add_image_codec_subdirectory source_dir binary_dir)
    set(_termin_previous_skip_install_rules "${CMAKE_SKIP_INSTALL_RULES}")
    set(CMAKE_SKIP_INSTALL_RULES TRUE)
    add_subdirectory("${source_dir}" "${binary_dir}" EXCLUDE_FROM_ALL)
    set(CMAKE_SKIP_INSTALL_RULES "${_termin_previous_skip_install_rules}" PARENT_SCOPE)
endfunction()

function(_termin_configure_bundled_image_codecs)
    include(ExternalProject)

    _termin_require_image_codec_source("zlib" "${TERMIN_ZLIB_SOURCE_DIR}" "CMakeLists.txt")
    _termin_require_image_codec_source("libpng" "${TERMIN_LIBPNG_SOURCE_DIR}" "CMakeLists.txt")
    _termin_require_image_codec_source("libjpeg-turbo" "${TERMIN_LIBJPEG_TURBO_SOURCE_DIR}" "CMakeLists.txt")
    _termin_require_image_codec_source("libwebp" "${TERMIN_LIBWEBP_SOURCE_DIR}" "CMakeLists.txt")

    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    if(NOT TARGET zlibstatic)
        _termin_add_image_codec_subdirectory(
            "${TERMIN_ZLIB_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/termin-thirdparty/zlib"
        )
    endif()
    if(TARGET zlibstatic AND NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
    set(ZLIB_INCLUDE_DIR "${TERMIN_ZLIB_SOURCE_DIR}" CACHE PATH "" FORCE)
    set(ZLIB_INCLUDE_DIRS "${TERMIN_ZLIB_SOURCE_DIR}")
    # libpng's upstream CMake runs FindZLIB even though the bundled target exists.
    # A target-valued library variable keeps that lookup self-contained until zlibstatic is built.
    set(ZLIB_LIBRARY ZLIB::ZLIB)
    set(ZLIB_LIBRARIES ZLIB::ZLIB)

    set(PNG_SHARED OFF CACHE BOOL "" FORCE)
    set(PNG_STATIC ON CACHE BOOL "" FORCE)
    set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
    set(PNG_TESTS OFF CACHE BOOL "" FORCE)
    set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON)
    if(NOT TARGET png_static)
        _termin_add_image_codec_subdirectory(
            "${TERMIN_LIBPNG_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/termin-thirdparty/libpng"
        )
    endif()

    set(_termin_libjpeg_turbo_binary_dir "${CMAKE_BINARY_DIR}/termin-thirdparty/libjpeg-turbo")
    set(_termin_libjpeg_turbo_install_dir "${_termin_libjpeg_turbo_binary_dir}/install")
    file(MAKE_DIRECTORY "${_termin_libjpeg_turbo_install_dir}/include")
    if(MSVC)
        set(_termin_libjpeg_turbo_library
            "${_termin_libjpeg_turbo_install_dir}/lib/jpeg-static${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
    else()
        set(_termin_libjpeg_turbo_library
            "${_termin_libjpeg_turbo_install_dir}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}jpeg${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
    endif()
    set(_termin_libjpeg_turbo_cmake_args
        "-DCMAKE_INSTALL_PREFIX=${_termin_libjpeg_turbo_install_dir}"
        "-DCMAKE_INSTALL_LIBDIR=lib"
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON"
        "-DENABLE_SHARED=OFF"
        "-DENABLE_STATIC=ON"
        "-DWITH_TURBOJPEG=OFF"
        "-DWITH_TOOLS=OFF"
        "-DWITH_TESTS=OFF"
        "-DWITH_FUZZ=OFF"
    )
    if(CMAKE_BUILD_TYPE)
        list(APPEND _termin_libjpeg_turbo_cmake_args "-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}")
    endif()
    if(DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        list(APPEND _termin_libjpeg_turbo_cmake_args
            "-DCMAKE_MSVC_RUNTIME_LIBRARY=${CMAKE_MSVC_RUNTIME_LIBRARY}"
        )
    endif()
    if(NOT TARGET termin_libjpeg_turbo_static)
        ExternalProject_Add(termin_libjpeg_turbo_ep
            SOURCE_DIR "${TERMIN_LIBJPEG_TURBO_SOURCE_DIR}"
            BINARY_DIR "${_termin_libjpeg_turbo_binary_dir}"
            INSTALL_DIR "${_termin_libjpeg_turbo_install_dir}"
            CMAKE_ARGS ${_termin_libjpeg_turbo_cmake_args}
            BUILD_BYPRODUCTS "${_termin_libjpeg_turbo_library}"
        )
        add_library(termin_libjpeg_turbo_static STATIC IMPORTED GLOBAL)
        set_target_properties(termin_libjpeg_turbo_static PROPERTIES
            IMPORTED_LOCATION "${_termin_libjpeg_turbo_library}"
            INTERFACE_INCLUDE_DIRECTORIES "${_termin_libjpeg_turbo_install_dir}/include"
        )
        add_dependencies(termin_libjpeg_turbo_static termin_libjpeg_turbo_ep)
    endif()

    set(WEBP_LINK_STATIC ON CACHE BOOL "" FORCE)
    set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_WEBP_JS OFF CACHE BOOL "" FORCE)
    set(WEBP_BUILD_FUZZTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF)
    if(NOT TARGET webpdecoder)
        _termin_add_image_codec_subdirectory(
            "${TERMIN_LIBWEBP_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/termin-thirdparty/libwebp"
        )
    endif()
    # The upstream target does not publish its source-tree API directory.
    # Consumers of the bundled decoder include <webp/decode.h> from this path.
    target_include_directories(webpdecoder INTERFACE "${TERMIN_LIBWEBP_SOURCE_DIR}/src")

    set(TERMIN_IMAGE_CODEC_TARGETS
        png_static
        termin_libjpeg_turbo_static
        webpdecoder
        PARENT_SCOPE
    )
    set(TERMIN_IMAGE_CODECS_BUNDLED ON PARENT_SCOPE)
    message(STATUS "termin-image codecs: using bundled zlib/libpng/libjpeg-turbo/libwebp")
endfunction()

function(_termin_find_system_image_codecs)
    set(_termin_system_codec_targets)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TERMIN_IMAGE_PNG QUIET IMPORTED_TARGET libpng)
        pkg_check_modules(TERMIN_IMAGE_JPEG QUIET IMPORTED_TARGET libjpeg)
        pkg_check_modules(TERMIN_IMAGE_WEBP QUIET IMPORTED_TARGET libwebp)
    endif()

    if(TARGET PkgConfig::TERMIN_IMAGE_PNG
            AND TARGET PkgConfig::TERMIN_IMAGE_JPEG
            AND TARGET PkgConfig::TERMIN_IMAGE_WEBP)
        set(_termin_system_codec_targets
            PkgConfig::TERMIN_IMAGE_PNG
            PkgConfig::TERMIN_IMAGE_JPEG
            PkgConfig::TERMIN_IMAGE_WEBP
        )
    else()
        find_package(PNG QUIET)
        find_package(JPEG QUIET)
        find_path(TERMIN_IMAGE_WEBP_INCLUDE_DIR webp/decode.h)
        find_library(TERMIN_IMAGE_WEBP_LIBRARY NAMES webp libwebp)
        if(TERMIN_IMAGE_WEBP_INCLUDE_DIR AND TERMIN_IMAGE_WEBP_LIBRARY
                AND NOT TARGET termin_image_webp_external)
            add_library(termin_image_webp_external UNKNOWN IMPORTED)
            set_target_properties(termin_image_webp_external PROPERTIES
                IMPORTED_LOCATION "${TERMIN_IMAGE_WEBP_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${TERMIN_IMAGE_WEBP_INCLUDE_DIR}"
            )
        endif()
        if(PNG_FOUND AND JPEG_FOUND AND TARGET termin_image_webp_external)
            set(_termin_system_codec_targets
                PNG::PNG
                JPEG::JPEG
                termin_image_webp_external
            )
        endif()
    endif()

    if(NOT _termin_system_codec_targets)
        message(FATAL_ERROR
            "termin-image requires PNG/JPEG/WebP codec libraries. "
            "Install system libpng, libjpeg and libwebp development packages "
            "or configure with -DTERMIN_USE_BUNDLED_IMAGE_CODECS=ON."
        )
    endif()

    set(TERMIN_IMAGE_CODEC_TARGETS ${_termin_system_codec_targets} PARENT_SCOPE)
    set(TERMIN_IMAGE_CODECS_BUNDLED OFF PARENT_SCOPE)
    message(STATUS "termin-image codecs: using system libpng/libjpeg/libwebp")
endfunction()

macro(termin_configure_image_codecs)
    set(TERMIN_IMAGE_CODEC_TARGETS)
    set(TERMIN_IMAGE_CODECS_BUNDLED OFF)
    if(TERMIN_USE_BUNDLED_IMAGE_CODECS)
        _termin_configure_bundled_image_codecs()
    else()
        _termin_find_system_image_codecs()
    endif()
endmacro()
