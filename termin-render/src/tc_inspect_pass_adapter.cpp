#include <inspect/tc_inspect_pass_adapter.h>
#include <render/tc_pass.h>
#include <termin/render/frame_pass.hpp>

extern "C" {

void tc_inspect_pass_adapter_init(void) {
}

tc_value tc_pass_inspect_get(tc_pass* p, const char* path) {
    if (!p || !path) return tc_value_nil();

    const char* type_name = tc_pass_type_name(p);
    if (!type_name) return tc_value_nil();

    void* obj = nullptr;
    if (p->kind == TC_NATIVE_PASS) {
        obj = termin::CxxFramePass::from_tc(p);
    } else {
        obj = p->body;
    }
    if (!obj) return tc_value_nil();

    return tc_inspect_get(obj, type_name, path);
}

void tc_pass_inspect_set(tc_pass* p, const char* path, tc_value value, void* context) {
    if (!p || !path) return;

    const char* type_name = tc_pass_type_name(p);
    if (!type_name) return;

    void* obj = nullptr;
    if (p->kind == TC_NATIVE_PASS) {
        obj = termin::CxxFramePass::from_tc(p);
    } else {
        obj = p->body;
    }
    if (!obj) return;

    tc_inspect_set(obj, type_name, path, value, context);
}

}
