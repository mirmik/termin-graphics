#pragma once

#if defined(_WIN32)
#  if defined(TERMIN_MATERIALS_EXPORTS)
#    define TERMIN_MATERIALS_API __declspec(dllexport)
#  else
#    define TERMIN_MATERIALS_API __declspec(dllimport)
#  endif
#else
#  define TERMIN_MATERIALS_API __attribute__((visibility("default")))
#endif
