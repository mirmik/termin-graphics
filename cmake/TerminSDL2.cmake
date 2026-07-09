include_guard(GLOBAL)

set(
    TERMIN_SDL2_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../termin-thirdparty/sdl2"
    CACHE INTERNAL "Termin bundled SDL2 source directory"
)

if(WIN32)
    set(_TERMIN_USE_BUNDLED_SDL2_DEFAULT ON)
else()
    set(_TERMIN_USE_BUNDLED_SDL2_DEFAULT OFF)
endif()

option(
    TERMIN_USE_BUNDLED_SDL2
    "Prefer the SDL2 source submodule from termin-thirdparty/sdl2 when SDL support is enabled"
    ${_TERMIN_USE_BUNDLED_SDL2_DEFAULT}
)

macro(termin_configure_sdl2)
    set(TERMIN_SDL2_BUNDLED OFF)
    set(SDL2_FOUND FALSE)
    set(SDL2_LIBRARIES)
    set(SDL2_INCLUDE_DIRS)
    set(SDL2_DLL)

    if(NOT USE_SYSTEM_SDL2)
        message(STATUS "SDL2 disabled")
    else()
        set(_termin_sdl2_source_dir "${TERMIN_SDL2_SOURCE_DIR}")
        if(TERMIN_USE_BUNDLED_SDL2 AND EXISTS "${_termin_sdl2_source_dir}/CMakeLists.txt")
            set(SDL_SHARED ON CACHE BOOL "" FORCE)
            set(SDL_STATIC OFF CACHE BOOL "" FORCE)
            set(SDL_TEST OFF CACHE BOOL "" FORCE)
            set(SDL_TESTS OFF CACHE BOOL "" FORCE)
            set(SDL_INSTALL_TESTS OFF CACHE BOOL "" FORCE)
            set(SDL2_DISABLE_INSTALL OFF CACHE BOOL "" FORCE)

            if(NOT TARGET SDL2::SDL2)
                add_subdirectory(
                    "${_termin_sdl2_source_dir}"
                    "${CMAKE_BINARY_DIR}/termin-thirdparty/sdl2"
                )
            endif()

            set(SDL2_FOUND TRUE)
            set(SDL2_LIBRARIES SDL2::SDL2)
            set(TERMIN_SDL2_BUNDLED ON)
            message(STATUS "SDL2: using bundled submodule at ${_termin_sdl2_source_dir}")
        elseif(WIN32)
            set(SDL2_SEARCH_PATHS
                "C:/SDL2"
                "$ENV{SDL2_DIR}"
                "$ENV{PROGRAMFILES}/SDL2"
            )
            find_path(SDL2_INCLUDE_DIRS SDL2/SDL.h
                PATH_SUFFIXES include
                PATHS ${SDL2_SEARCH_PATHS}
            )
            find_library(SDL2_LIBRARY
                NAMES SDL2
                PATH_SUFFIXES lib/x64 lib
                PATHS ${SDL2_SEARCH_PATHS}
            )
            if(SDL2_INCLUDE_DIRS AND SDL2_LIBRARY)
                set(SDL2_FOUND TRUE)
                set(SDL2_LIBRARIES ${SDL2_LIBRARY})
                get_filename_component(SDL2_LIB_DIR ${SDL2_LIBRARY} DIRECTORY)
                find_file(SDL2_DLL
                    NAMES SDL2.dll
                    PATHS ${SDL2_LIB_DIR} ${SDL2_LIB_DIR}/../bin ${SDL2_LIB_DIR}/..
                    NO_DEFAULT_PATH
                )
                message(STATUS "SDL2: using external Windows install: ${SDL2_INCLUDE_DIRS}")
                message(STATUS "SDL2 DLL: ${SDL2_DLL}")
            else()
                message(STATUS "SDL2 not found, disabling SDL2 support")
                set(SDL2_FOUND FALSE)
            endif()
        else()
            find_package(SDL2 QUIET)
            if(SDL2_FOUND AND TARGET SDL2::SDL2)
                set(SDL2_LIBRARIES SDL2::SDL2)
            endif()
            if(NOT SDL2_FOUND)
                find_package(PkgConfig QUIET)
                if(PkgConfig_FOUND)
                    pkg_check_modules(SDL2 QUIET sdl2)
                endif()
            endif()
            if(SDL2_FOUND)
                message(STATUS "SDL2: using system package")
            else()
                message(STATUS "SDL2 not found, disabling SDL2 support")
            endif()
        endif()
    endif()
endmacro()

function(termin_install_sdl2_runtime destination)
    if(WIN32 AND SDL2_DLL AND NOT TERMIN_SDL2_BUNDLED)
        install(FILES "${SDL2_DLL}" DESTINATION "${destination}")
    endif()
endfunction()

function(termin_copy_sdl2_runtime target)
    if(NOT WIN32)
        return()
    endif()
    if(TARGET SDL2)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:SDL2>"
                "$<TARGET_FILE_DIR:${target}>"
        )
    elseif(SDL2_DLL)
        add_custom_command(TARGET "${target}" POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${SDL2_DLL}"
                "$<TARGET_FILE_DIR:${target}>"
        )
    endif()
endfunction()
