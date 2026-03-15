# TerminRpath.cmake — unified RPATH helpers for termin-env projects.
#
# Usage:
#   list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../cmake")
#   include(TerminRpath)
#
#   termin_set_rpath_lib(my_shared_lib)
#   termin_set_rpath_python(my_nanobind_module)

# For native shared libraries (.so) installed into lib/
function(termin_set_rpath_lib target)
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "$ORIGIN;${CMAKE_INSTALL_PREFIX}/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endfunction()

# For nanobind Python modules installed into lib/python/termin/<pkg>/
function(termin_set_rpath_python target)
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "$ORIGIN;$ORIGIN/..;$ORIGIN/../..;$ORIGIN/../../..;${CMAKE_INSTALL_PREFIX}/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endfunction()

# For pip-installed Python modules (.so next to package's lib/ dir)
function(termin_set_rpath_pip target)
    if(NOT WIN32)
        set_target_properties(${target} PROPERTIES
            INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_PREFIX}/lib"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endfunction()
