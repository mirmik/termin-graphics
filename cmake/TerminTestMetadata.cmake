include_guard(GLOBAL)

# Keep CTest metadata machine-readable.  The repository planner uses these
# labels to join CTest's concrete registrations back to the module/test-suite
# inventory; do not replace them with ad-hoc target-name parsing in scripts.
function(termin_label_tests_in_directory module)
    get_property(_termin_directory_tests DIRECTORY PROPERTY TESTS)
    foreach(_termin_test IN LISTS _termin_directory_tests)
        set_property(TEST "${_termin_test}" APPEND PROPERTY LABELS
            "termin:module:${module}"
            "termin:tier:automatic"
            "termin:capability:host")
        if(TARGET "${_termin_test}")
            termin_set_test_build_target("${_termin_test}" "${_termin_test}")
        endif()

        get_property(_termin_labels TEST "${_termin_test}" PROPERTY LABELS)
        set(_termin_build_target "")
        set(_termin_requires_python_bindings FALSE)
        set(_termin_requires_window FALSE)
        foreach(_termin_label IN LISTS _termin_labels)
            if(_termin_label MATCHES "^termin:build-target:(.+)$")
                if(_termin_build_target)
                    message(FATAL_ERROR
                        "CTest registration ${_termin_test} has multiple "
                        "termin:build-target labels")
                endif()
                set(_termin_build_target "${CMAKE_MATCH_1}")
            elseif(_termin_label STREQUAL
                   "termin:capability:python-bindings")
                set(_termin_requires_python_bindings TRUE)
            elseif(_termin_label STREQUAL "termin:capability:window")
                set(_termin_requires_window TRUE)
            endif()
        endforeach()
        if(NOT _termin_build_target)
            message(FATAL_ERROR
                "CTest registration ${_termin_test} has no build target")
        endif()
        if(NOT _termin_requires_python_bindings)
            set_property(
                GLOBAL APPEND PROPERTY
                TERMIN_NATIVE_TEST_TARGETS_WITH_WINDOW
                "${_termin_build_target}"
            )
            if(NOT _termin_requires_window)
                set_property(
                    GLOBAL APPEND PROPERTY
                    TERMIN_NATIVE_TEST_TARGETS
                    "${_termin_build_target}"
                )
            endif()
        endif()
    endforeach()
endfunction()

function(termin_add_test_labels test)
    set_property(TEST "${test}" APPEND PROPERTY LABELS ${ARGN})
endfunction()

function(termin_set_test_build_target test target)
    if(NOT TARGET "${target}")
        message(FATAL_ERROR
            "CTest registration ${test} names unknown build target ${target}")
    endif()
    set_property(TEST "${test}" APPEND PROPERTY LABELS
        "termin:build-target:${target}")
endfunction()
