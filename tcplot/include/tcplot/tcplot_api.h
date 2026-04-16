#ifndef TCPLOT_API_H
#define TCPLOT_API_H

#ifdef _WIN32
    #ifdef TCPLOT_EXPORTS
        #define TCPLOT_API __declspec(dllexport)
    #else
        #define TCPLOT_API __declspec(dllimport)
    #endif
#else
    #if defined(__clang__)
        #define TCPLOT_API __attribute__((visibility("default"), type_visibility("default")))
    #elif defined(__GNUC__)
        #define TCPLOT_API __attribute__((visibility("default")))
    #else
        #define TCPLOT_API
    #endif
#endif

#endif  // TCPLOT_API_H
