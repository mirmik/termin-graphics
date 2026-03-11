#include <termin/render/frame_pass.hpp>
#include <termin/render/tc_pass.hpp>

extern "C" {
#include "inspect/tc_inspect.h"
#include "inspect/tc_inspect_pass_adapter.h"
}

namespace termin {

void* TcPassRef::object_ptr() const {
    if (!_c) return nullptr;

    if (_c->kind == TC_NATIVE_PASS) {
        return CxxFramePass::from_tc(_c);
    }

    return _c->body;
}

bool TcPassRef::set_field(const std::string& field_name, const tc_value& value) {
    if (!_c) return false;

    const char* type = tc_pass_type_name(_c);
    if (!type) return false;

    tc_field_info info;
    if (!tc_inspect_find_field_info(type, field_name.c_str(), &info)) {
        return false;
    }

    tc_pass_inspect_set(_c, field_name.c_str(), value, nullptr);
    return true;
}

} // namespace termin
