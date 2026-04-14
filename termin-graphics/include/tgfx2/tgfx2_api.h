#ifndef TGFX2_API_H
#define TGFX2_API_H

// Visibility: nanobind extension modules are compiled with
// -fvisibility=hidden, which hides each module's local copy of
// typeid(T) for classes without virtual methods. When one nanobind
// module (e.g. _tgfx_native) binds a tgfx2 class and another module
// (e.g. _render_framework_native) tries to return/accept it, the
// cross-module type lookup in nanobind uses typeid comparison. If
// each module has its own hidden typeinfo symbol, the lookup fails
// with "Unable to convert function return value to a Python type".
//
// Fix: give every TGFX2_API class default visibility AND default
// type_visibility so the typeinfo and vtable (when present) are
// exported from libtermin_graphics2.so and shared across all
// consumers. No runtime cost — this is purely a symbol visibility
// change for the compiler/linker.
#ifdef _WIN32
    #ifdef TGFX2_EXPORTS
        #define TGFX2_API __declspec(dllexport)
    #else
        #define TGFX2_API __declspec(dllimport)
    #endif
#else
    // type_visibility is clang-only; gcc doesn't need it — its
    // visibility attribute on the class already covers typeinfo.
    #if defined(__clang__)
        #define TGFX2_API __attribute__((visibility("default"), type_visibility("default")))
    #elif defined(__GNUC__)
        #define TGFX2_API __attribute__((visibility("default")))
    #else
        #define TGFX2_API
    #endif
#endif

#endif // TGFX2_API_H
