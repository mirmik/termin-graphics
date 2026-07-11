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
    endforeach()
endfunction()

function(termin_add_test_labels test)
    set_property(TEST "${test}" APPEND PROPERTY LABELS ${ARGN})
endfunction()
