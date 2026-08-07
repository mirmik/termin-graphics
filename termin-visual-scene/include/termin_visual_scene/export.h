#pragma once

#if defined(_WIN32)
#if defined(TERMIN_VISUAL_SCENE_EXPORTS)
#define TERMIN_VISUAL_SCENE_API __declspec(dllexport)
#else
#define TERMIN_VISUAL_SCENE_API __declspec(dllimport)
#endif
#else
#define TERMIN_VISUAL_SCENE_API __attribute__((visibility("default")))
#endif
