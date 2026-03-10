#ifndef TC_DRAWABLE_CAPABILITY_H
#define TC_DRAWABLE_CAPABILITY_H

#include "core/tc_component_capability.h"

#ifdef __cplusplus
extern "C" {
#endif

struct tc_component;
struct tc_drawable_vtable;

typedef struct tc_drawable_capability {
    const struct tc_drawable_vtable* vtable;
    void* userdata;
} tc_drawable_capability;

TC_API tc_component_cap_id tc_drawable_capability_id(void);
TC_API bool tc_drawable_capability_attach(struct tc_component* c, const struct tc_drawable_vtable* vtable, void* userdata);
TC_API const tc_drawable_capability* tc_drawable_capability_get(const struct tc_component* c);

#ifdef __cplusplus
}
#endif

#endif
