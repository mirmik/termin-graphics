include_guard(DIRECTORY)

set(TERMIN_CANONICAL_PYTHON_VERSION "3.14")

macro(termin_require_canonical_python)
    find_package(
        Python ${TERMIN_CANONICAL_PYTHON_VERSION}
        COMPONENTS ${ARGN}
        REQUIRED
    )

    execute_process(
        COMMAND "${Python_EXECUTABLE}" -I -c
            "import sys, sysconfig; print(f'{sys.version_info.major}.{sys.version_info.minor}|{sysconfig.get_config_var(\"SOABI\") or \"\"}|{int(bool(sysconfig.get_config_var(\"Py_GIL_DISABLED\") or 0))}')"
        OUTPUT_VARIABLE TERMIN_PYTHON_ABI_PROBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE TERMIN_PYTHON_ABI_PROBE_RESULT
        ERROR_VARIABLE TERMIN_PYTHON_ABI_PROBE_ERROR
    )
    if(NOT TERMIN_PYTHON_ABI_PROBE_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to inspect canonical Python ABI for ${Python_EXECUTABLE}: "
            "${TERMIN_PYTHON_ABI_PROBE_ERROR}")
    endif()

    string(REPLACE "|" ";" TERMIN_PYTHON_ABI_FIELDS
        "${TERMIN_PYTHON_ABI_PROBE}")
    list(LENGTH TERMIN_PYTHON_ABI_FIELDS TERMIN_PYTHON_ABI_FIELD_COUNT)
    if(NOT TERMIN_PYTHON_ABI_FIELD_COUNT EQUAL 3)
        message(FATAL_ERROR
            "Malformed canonical Python ABI probe for ${Python_EXECUTABLE}: "
            "${TERMIN_PYTHON_ABI_PROBE}")
    endif()

    list(GET TERMIN_PYTHON_ABI_FIELDS 0 TERMIN_PYTHON_ABI_VERSION)
    list(GET TERMIN_PYTHON_ABI_FIELDS 1 TERMIN_PYTHON_SOABI)
    list(GET TERMIN_PYTHON_ABI_FIELDS 2 TERMIN_PYTHON_GIL_DISABLED)
    if(NOT TERMIN_PYTHON_ABI_VERSION STREQUAL
       TERMIN_CANONICAL_PYTHON_VERSION)
        message(FATAL_ERROR
            "Termin requires CPython ${TERMIN_CANONICAL_PYTHON_VERSION}t, "
            "got ${TERMIN_PYTHON_ABI_VERSION} (${TERMIN_PYTHON_SOABI}) from "
            "${Python_EXECUTABLE}")
    endif()
    if(NOT TERMIN_PYTHON_GIL_DISABLED STREQUAL "1"
       OR NOT TERMIN_PYTHON_SOABI MATCHES "^cpython-314t($|-)")
        message(FATAL_ERROR
            "Termin requires free-threaded CPython "
            "${TERMIN_CANONICAL_PYTHON_VERSION}t, got "
            "${TERMIN_PYTHON_SOABI} from ${Python_EXECUTABLE}")
    endif()

    set(TERMIN_PYTHON_STDLIB_DIR_NAME
        "python${TERMIN_CANONICAL_PYTHON_VERSION}t")
    set(TERMIN_PYTHON_ABI_DIR_SUFFIX
        "${TERMIN_CANONICAL_PYTHON_VERSION}t")
    set(Python_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH
        "Canonical free-threaded Python used by Termin" FORCE)
endmacro()
