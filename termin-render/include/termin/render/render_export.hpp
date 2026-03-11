#pragma once

#ifdef _WIN32
    #ifdef TERMIN_RENDER_EXPORTS
        #define RENDER_API __declspec(dllexport)
    #else
        #define RENDER_API __declspec(dllimport)
    #endif
#else
    #define RENDER_API
#endif
