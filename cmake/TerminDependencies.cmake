function(termin_require_package package)
    set(_missing_target OFF)

    if(NOT ARGN)
        set(_missing_target ON)
    endif()

    foreach(_target IN LISTS ARGN)
        if(NOT TARGET "${_target}")
            set(_missing_target ON)
        endif()
    endforeach()

    if(_missing_target)
        find_package(${package} REQUIRED)
    endif()
endfunction()

function(termin_add_alias_if_missing alias target)
    if(NOT TARGET "${alias}" AND TARGET "${target}")
        add_library("${alias}" ALIAS "${target}")
    endif()
endfunction()
