#pragma once

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef TERMIN_GUI_NATIVE_RENDERER_EXPORTS
#    define TERMIN_GUI_NATIVE_RENDERER_API __declspec(dllexport)
#  else
#    define TERMIN_GUI_NATIVE_RENDERER_API __declspec(dllimport)
#  endif
#else
#  define TERMIN_GUI_NATIVE_RENDERER_API __attribute__((visibility("default")))
#endif
