#ifndef TGFX2_API_H
#define TGFX2_API_H

#ifdef _WIN32
    #ifdef TGFX2_EXPORTS
        #define TGFX2_API __declspec(dllexport)
    #else
        #define TGFX2_API __declspec(dllimport)
    #endif
#else
    #define TGFX2_API
#endif

#endif // TGFX2_API_H
