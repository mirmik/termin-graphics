#pragma once

#ifdef _WIN32
#    ifdef TCPLOT_GUI_NATIVE_EXPORTS
#        define TCPLOT_GUI_NATIVE_API __declspec(dllexport)
#    else
#        define TCPLOT_GUI_NATIVE_API __declspec(dllimport)
#    endif
#else
#    define TCPLOT_GUI_NATIVE_API __attribute__((visibility("default")))
#endif
