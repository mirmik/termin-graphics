#ifndef TERMIN_WINDOW_API_H
#define TERMIN_WINDOW_API_H

#ifdef _WIN32
    #ifdef TERMIN_WINDOW_EXPORTS
        #define TERMIN_WINDOW_API __declspec(dllexport)
    #else
        #define TERMIN_WINDOW_API __declspec(dllimport)
    #endif
#else
    #define TERMIN_WINDOW_API
#endif

#endif // TERMIN_WINDOW_API_H
