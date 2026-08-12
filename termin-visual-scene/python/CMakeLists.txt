find_package(Python 3.14 COMPONENTS Interpreter Development REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(_visual_scene_native NB_SHARED
    bindings/visual_scene_bindings.cpp
    bindings/visual_scene3d_bindings.cpp
)
target_link_libraries(_visual_scene_native PRIVATE
    termin_visual_scene
    tcbase::termin_base
    tgfx::termin_graphics2
)
target_include_directories(_visual_scene_native PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

if(NOT WIN32)
    set_target_properties(_visual_scene_native PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN/lib;${CMAKE_INSTALL_PREFIX}/lib"
    )
endif()

install(TARGETS _visual_scene_native
    DESTINATION lib/python/termin/visual_scene)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/termin/visual_scene/
    DESTINATION lib/python/termin/visual_scene
    PATTERN "__pycache__" EXCLUDE
    PATTERN "*.pyc" EXCLUDE
)
