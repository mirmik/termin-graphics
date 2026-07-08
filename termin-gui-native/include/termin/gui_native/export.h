#ifndef TERMIN_GUI_NATIVE_EXPORT_H
#define TERMIN_GUI_NATIVE_EXPORT_H

#ifdef _WIN32
#    ifdef TERMIN_GUI_NATIVE_EXPORTS
#        define TERMIN_GUI_NATIVE_API __declspec(dllexport)
#    else
#        define TERMIN_GUI_NATIVE_API __declspec(dllimport)
#    endif
#else
#    define TERMIN_GUI_NATIVE_API __attribute__((visibility("default")))
#endif

#endif // TERMIN_GUI_NATIVE_EXPORT_H
