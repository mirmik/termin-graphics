#pragma once

#if defined(_WIN32)
#if defined(TERMIN_NODEGRAPH_CORE_EXPORTS)
#define TERMIN_NODEGRAPH_CORE_API __declspec(dllexport)
#else
#define TERMIN_NODEGRAPH_CORE_API __declspec(dllimport)
#endif
#else
#define TERMIN_NODEGRAPH_CORE_API __attribute__((visibility("default")))
#endif

