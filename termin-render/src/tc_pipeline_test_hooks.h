#ifndef TC_PIPELINE_TEST_HOOKS_H
#define TC_PIPELINE_TEST_HOOKS_H

#include <stddef.h>
#include <tcbase/types/api.h>

#ifdef __cplusplus
extern "C" {
#endif

TC_API void tc_pipeline_test_fail_storage_allocation_after(size_t successful_allocations);
TC_API void tc_pipeline_test_reset_storage_allocator(void);

#ifdef __cplusplus
}
#endif

#endif
