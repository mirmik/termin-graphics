include_guard(DIRECTORY)

set(TERMIN_CANONICAL_PYTHON_VERSION "3.14")

macro(termin_require_canonical_python)
    # CMake versions predating free-threaded CPython support do not search for
    # the "t" ABI suffix (for example, python314t.lib). Find the interpreter
    # first, then seed its exact development artifacts from sysconfig. This
    # also prevents an interpreter from being paired with headers or a library
    # from another Python installation.
    find_package(
        Python ${TERMIN_CANONICAL_PYTHON_VERSION}
        COMPONENTS Interpreter
        REQUIRED
    )

    execute_process(
        COMMAND "${Python_EXECUTABLE}" -I -c
            "import pathlib, sys, sysconfig; libdir = pathlib.Path(sysconfig.get_config_var('LIBDIR') or ''); library = pathlib.Path(sysconfig.get_config_var('LDLIBRARY') or ''); library = library.with_suffix('.lib') if sys.platform == 'win32' else library; print(f'{sys.version_info.major}.{sys.version_info.minor}|{sysconfig.get_config_var(\"SOABI\") or \"\"}|{int(bool(sysconfig.get_config_var(\"Py_GIL_DISABLED\") or 0))}|{sysconfig.get_path(\"include\") or \"\"}|{libdir / library}')"
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
    if(NOT TERMIN_PYTHON_ABI_FIELD_COUNT EQUAL 5)
        message(FATAL_ERROR
            "Malformed canonical Python ABI probe for ${Python_EXECUTABLE}: "
            "${TERMIN_PYTHON_ABI_PROBE}")
    endif()

    list(GET TERMIN_PYTHON_ABI_FIELDS 0 TERMIN_PYTHON_ABI_VERSION)
    list(GET TERMIN_PYTHON_ABI_FIELDS 1 TERMIN_PYTHON_SOABI)
    list(GET TERMIN_PYTHON_ABI_FIELDS 2 TERMIN_PYTHON_GIL_DISABLED)
    list(GET TERMIN_PYTHON_ABI_FIELDS 3 TERMIN_PYTHON_INCLUDE_DIR)
    list(GET TERMIN_PYTHON_ABI_FIELDS 4 TERMIN_PYTHON_LIBRARY)
    if(NOT TERMIN_PYTHON_ABI_VERSION STREQUAL
       TERMIN_CANONICAL_PYTHON_VERSION)
        message(FATAL_ERROR
            "Termin requires CPython ${TERMIN_CANONICAL_PYTHON_VERSION}t, "
            "got ${TERMIN_PYTHON_ABI_VERSION} (${TERMIN_PYTHON_SOABI}) from "
            "${Python_EXECUTABLE}")
    endif()
    if(NOT TERMIN_PYTHON_GIL_DISABLED STREQUAL "1"
       OR NOT TERMIN_PYTHON_SOABI MATCHES "^(cpython-|cp)314t($|-)")
        message(FATAL_ERROR
            "Termin requires free-threaded CPython "
            "${TERMIN_CANONICAL_PYTHON_VERSION}t, got "
            "${TERMIN_PYTHON_SOABI} from ${Python_EXECUTABLE}")
    endif()
    if(NOT EXISTS "${TERMIN_PYTHON_INCLUDE_DIR}/Python.h")
        message(FATAL_ERROR
            "Canonical Python headers were not found for "
            "${Python_EXECUTABLE}: ${TERMIN_PYTHON_INCLUDE_DIR}/Python.h")
    endif()
    if(NOT EXISTS "${TERMIN_PYTHON_LIBRARY}")
        message(FATAL_ERROR
            "Canonical Python library was not found for "
            "${Python_EXECUTABLE}: ${TERMIN_PYTHON_LIBRARY}")
    endif()

    set(TERMIN_PYTHON_STDLIB_DIR_NAME
        "python${TERMIN_CANONICAL_PYTHON_VERSION}t")
    set(TERMIN_PYTHON_ABI_DIR_SUFFIX
        "${TERMIN_CANONICAL_PYTHON_VERSION}t")
    set(Python_EXECUTABLE "${Python_EXECUTABLE}" CACHE FILEPATH
        "Canonical free-threaded Python used by Termin" FORCE)
    set(Python_INCLUDE_DIR "${TERMIN_PYTHON_INCLUDE_DIR}" CACHE PATH
        "Canonical free-threaded Python headers used by Termin" FORCE)
    set(Python_LIBRARY "${TERMIN_PYTHON_LIBRARY}" CACHE FILEPATH
        "Canonical free-threaded Python library used by Termin" FORCE)

    find_package(
        Python ${TERMIN_CANONICAL_PYTHON_VERSION}
        COMPONENTS ${ARGN}
        REQUIRED
    )

    # On Windows, Py_GIL_DISABLED is supplied by the extension build system
    # rather than defined unconditionally by pyconfig.h. CMake versions that
    # predate free-threaded Python do not propagate it. Attach the canonical
    # ABI explicitly to every development target they created. Disable
    # Python.h's MSVC auto-link pragma as well: CMake already carries the
    # exact checked import library, while the pragma derives its name from
    # compile definitions and can silently request the regular-GIL library.
    foreach(_termin_python_target Python::Python Python::Module)
        if(TARGET "${_termin_python_target}")
            set_property(
                TARGET "${_termin_python_target}"
                APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                    Py_GIL_DISABLED=1
                    Py_NO_LINK_LIB
            )
        endif()
    endforeach()

    # nanobind persists interpreter-derived ABI values as INTERNAL cache
    # entries. Recompute them before the root nanobind target is configured so
    # an existing build tree cannot retain a suffix from the previous runtime.
    # Subprojects invoke this macro again after nanobind is loaded, so clearing
    # the values unconditionally would erase the active configuration.
    if(NOT TARGET nanobind-ft
       AND DEFINED NB_SUFFIX
       AND NOT NB_SUFFIX MATCHES "^\\.(cpython-|cp)314t")
        unset(NB_SOABI CACHE)
        unset(NB_SUFFIX CACHE)
        unset(NB_SUFFIX_S CACHE)
        unset(NB_ABI CACHE)
        unset(NB_FREE_THREADED CACHE)
    endif()
endmacro()
