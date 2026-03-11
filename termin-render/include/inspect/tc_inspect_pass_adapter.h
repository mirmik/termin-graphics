#ifndef TC_INSPECT_PASS_ADAPTER_H
#define TC_INSPECT_PASS_ADAPTER_H

#include <inspect/tc_inspect.h>
#include <inspect/tc_inspect_context.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tc_pass;

TC_API tc_value tc_pass_inspect_get(struct tc_pass* p, const char* path);
TC_API void tc_pass_inspect_set(struct tc_pass* p, const char* path, tc_value value, void* context);

#ifdef __cplusplus
}
#endif

#endif
