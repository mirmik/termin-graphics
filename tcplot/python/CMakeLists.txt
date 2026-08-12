find_package(Python 3.14 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)
if(TGFX2_ENABLE_OPENGL)
    find_package(OpenGL REQUIRED)
endif()

set(TCPLOT_PYTHON_BINDING_SOURCES
    bindings/tcplot_module.cpp
    bindings/colors_and_data_bindings.cpp
    bindings/camera_bindings.cpp
    bindings/annotation_bindings.cpp
    bindings/engine_bindings.cpp
    bindings/retained_chart3d_bindings.cpp
)

# CMake normally invalidates objects from source mtimes. Source trees restored
# by archive/cache tooling can preserve an older mtime even when their content
# changed, which previously left an obsolete _tcplot_native API in the SDK.
# Keep a content-derived dependency alongside the normal source dependency so
# reconfiguration invalidates every binding object when the binding source set
# changes, independent of filesystem timestamps.
set(_tcplot_binding_fingerprint_input "")
foreach(_tcplot_binding_source IN LISTS TCPLOT_PYTHON_BINDING_SOURCES)
    file(SHA256
        "${CMAKE_CURRENT_SOURCE_DIR}/${_tcplot_binding_source}"
        _tcplot_binding_source_hash)
    string(APPEND _tcplot_binding_fingerprint_input
        "${_tcplot_binding_source}:${_tcplot_binding_source_hash}\n")
endforeach()
string(SHA256 TCPLOT_BINDING_SOURCE_FINGERPRINT
    "${_tcplot_binding_fingerprint_input}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings_source_fingerprint.in"
    "${CMAKE_CURRENT_BINARY_DIR}/bindings_source_fingerprint"
    @ONLY)
set_source_files_properties(${TCPLOT_PYTHON_BINDING_SOURCES} PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/bindings_source_fingerprint")

nanobind_add_module(_tcplot_native NB_SHARED
    ${TCPLOT_PYTHON_BINDING_SOURCES}
)

target_link_libraries(_tcplot_native PRIVATE
    tcplot
    tgfx::termin_graphics2
    tmesh::termin_mesh
    tcbase::termin_base
)
if(TGFX2_ENABLE_OPENGL)
    target_link_libraries(_tcplot_native PRIVATE OpenGL::GL)
endif()
target_include_directories(_tcplot_native PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(NOT WIN32)
    set_target_properties(_tcplot_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_PREFIX}/lib"
    )
endif()

install(TARGETS _tcplot_native DESTINATION lib/python/tcplot)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tcplot/
    DESTINATION lib/python/tcplot
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)
