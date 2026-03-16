#ifndef TC_INSPECT_PASS_ADAPTER_H
#define TC_INSPECT_PASS_ADAPTER_H

#include <inspect/tc_inspect.h>
#include <inspect/tc_inspect_context.h>
#include <termin/render/render_export.hpp>

#ifdef __cplusplus
extern "C" {
#endif

struct tc_pass;

RENDER_API void tc_inspect_pass_adapter_init(void);
RENDER_API tc_value tc_pass_inspect_get(struct tc_pass* p, const char* path);
RENDER_API void tc_pass_inspect_set(struct tc_pass* p, const char* path, tc_value value, void* context);

#ifdef __cplusplus
}
#endif

#endif
