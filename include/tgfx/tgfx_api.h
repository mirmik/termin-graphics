#ifndef TGFX_API_H
#define TGFX_API_H

#ifdef _WIN32
    #ifdef TGFX_EXPORTS
        #define TGFX_API __declspec(dllexport)
    #else
        #define TGFX_API __declspec(dllimport)
    #endif
#else
    #define TGFX_API
#endif

#endif // TGFX_API_H
