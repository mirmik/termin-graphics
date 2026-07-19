#include <termin/render/frame_pass.hpp>

#include <tcbase/tc_log.h>

namespace termin {

FramePassTypeDescriptorBuilder::FramePassTypeDescriptorBuilder(
    const char* type_name,
    const char* owner,
    const char* parent,
    tc_pass_factory factory,
    void* factory_userdata,
    tc_pass_kind kind
)
    : _inspect(type_name ? type_name : ""),
      _type_name(type_name ? type_name : ""),
      _owner(owner ? owner : "") {
    if (_type_name.empty() || _owner.empty()) {
        tc_log(TC_LOG_ERROR, "[FramePassTypeDescriptor] type and owner must be non-empty");
        _valid = false;
        return;
    }
    if (tc_pass_registry_has(_type_name.c_str())) {
        const char* existing_owner = tc_runtime_type_registry_get_owner(_type_name.c_str());
        if (existing_owner && _owner == existing_owner) {
            _already_registered = true;
            return;
        }
        tc_log(
            TC_LOG_ERROR,
            "[FramePassTypeDescriptor] type '%s' is already registered by owner '%s'",
            _type_name.c_str(),
            existing_owner ? existing_owner : "<none>");
        _valid = false;
        return;
    }
    _descriptor = tc_runtime_type_descriptor_create(
        _type_name.c_str(), _owner.c_str(), parent && parent[0] ? parent : nullptr);
    if (_descriptor &&
        tc::InspectRegistry::instance().is_empty_unowned_type_shell(_type_name) &&
        !tc_runtime_type_descriptor_allow_unowned_shell_adoption(_descriptor)) {
        _valid = false;
    }
    if (!_descriptor || !tc_pass_type_descriptor_add_facet(
            _descriptor, factory, factory_userdata, kind)) {
        tc_log(TC_LOG_ERROR, "[FramePassTypeDescriptor] failed to stage pass facet for '%s'", _type_name.c_str());
        _valid = false;
    }
}

FramePassTypeDescriptorBuilder::~FramePassTypeDescriptorBuilder() {
    tc_runtime_type_descriptor_destroy(_descriptor);
}

FramePassTypeDescriptorBuilder::FramePassTypeDescriptorBuilder(
    FramePassTypeDescriptorBuilder&& other) noexcept
    : _descriptor(other._descriptor),
      _inspect(std::move(other._inspect)),
      _type_name(std::move(other._type_name)),
      _owner(std::move(other._owner)),
      _already_registered(other._already_registered),
      _valid(other._valid) {
    other._descriptor = nullptr;
}

FramePassTypeDescriptorBuilder& FramePassTypeDescriptorBuilder::operator=(
    FramePassTypeDescriptorBuilder&& other) noexcept {
    if (this == &other) return *this;
    tc_runtime_type_descriptor_destroy(_descriptor);
    _descriptor = other._descriptor;
    _inspect = std::move(other._inspect);
    _type_name = std::move(other._type_name);
    _owner = std::move(other._owner);
    _already_registered = other._already_registered;
    _valid = other._valid;
    other._descriptor = nullptr;
    return *this;
}

bool FramePassTypeDescriptorBuilder::commit() {
    if (_already_registered) return true;
    if (!_valid || !_descriptor || !_inspect.valid()) {
        tc_log(TC_LOG_ERROR, "[FramePassTypeDescriptor] invalid descriptor for '%s'", _type_name.c_str());
        return false;
    }
    if (!_inspect.attach_to(_descriptor)) {
        tc_log(TC_LOG_ERROR, "[FramePassTypeDescriptor] failed to stage inspect facet for '%s'", _type_name.c_str());
        return false;
    }
    tc_runtime_type_descriptor* descriptor = _descriptor;
    _descriptor = nullptr;
    if (!tc_runtime_type_registry_commit_descriptor(descriptor)) {
        tc_log(TC_LOG_ERROR, "[FramePassTypeDescriptor] failed to commit '%s'", _type_name.c_str());
        return false;
    }
    return true;
}

void CxxFramePass::_cb_execute(tc_pass* p, void* ctx) {
    CxxFramePass* self = from_tc(p);
    if (!self || !ctx) return;

    ExecuteContext* cpp_ctx = static_cast<ExecuteContext*>(ctx);
    self->execute(*cpp_ctx);
}

size_t CxxFramePass::_cb_get_reads(tc_pass* p, const char** out, size_t max) {
    CxxFramePass* self = from_tc(p);
    if (!self) return 0;

    auto computed_reads = self->compute_reads();
    size_t count = out ? std::min(computed_reads.size(), max) : 0;
    size_t i = 0;
    for (const char* r : computed_reads) {
        if (i >= count) break;
        out[i++] = r;
    }
    return computed_reads.size();
}

size_t CxxFramePass::_cb_get_writes(tc_pass* p, const char** out, size_t max) {
    CxxFramePass* self = from_tc(p);
    if (!self) return 0;

    auto computed_writes = self->compute_writes();
    size_t count = out ? std::min(computed_writes.size(), max) : 0;
    size_t i = 0;
    for (const char* w : computed_writes) {
        if (i >= count) break;
        out[i++] = w;
    }
    return computed_writes.size();
}

size_t CxxFramePass::_cb_get_inplace_aliases(tc_pass* p, const char** out, size_t max) {
    CxxFramePass* self = from_tc(p);
    if (!self) return 0;

    auto aliases = self->get_inplace_aliases();
    self->_cached_aliases.clear();
    self->_cached_aliases.reserve(aliases.size() * 2);

    for (const auto& [read_name, write_name] : aliases) {
        self->_cached_aliases.push_back(read_name);
        self->_cached_aliases.push_back(write_name);
    }

    size_t pair_count = out ? std::min(aliases.size(), max) : 0;
    for (size_t i = 0; i < pair_count; i++) {
        out[i * 2] = self->_cached_aliases[i * 2].c_str();
        out[i * 2 + 1] = self->_cached_aliases[i * 2 + 1].c_str();
    }
    return aliases.size();
}

size_t CxxFramePass::_cb_get_resource_specs(tc_pass* p, void* out, size_t max) {
    CxxFramePass* self = from_tc(p);
    if (!self) return 0;

    auto specs = self->get_resource_specs();
    if (!out) return specs.size();

    ResourceSpec* out_specs = static_cast<ResourceSpec*>(out);
    size_t count = std::min(specs.size(), max);

    for (size_t i = 0; i < count; i++) {
        out_specs[i] = specs[i];
    }

    return specs.size();
}

size_t CxxFramePass::_cb_get_internal_symbols(tc_pass* p, const char** out, size_t max) {
    CxxFramePass* self = from_tc(p);
    if (!self) return 0;

    self->_cached_symbols = self->get_internal_symbols();
    size_t count = out ? std::min(self->_cached_symbols.size(), max) : 0;
    for (size_t i = 0; i < count; i++) {
        out[i] = self->_cached_symbols[i].c_str();
    }
    return self->_cached_symbols.size();
}

void CxxFramePass::_cb_destroy(tc_pass* p) {
    CxxFramePass* self = from_tc(p);
    if (self) {
        self->destroy();
    }
}

const tc_pass_vtable CxxFramePass::_cpp_vtable = {
    .execute = CxxFramePass::_cb_execute,
    .get_reads = CxxFramePass::_cb_get_reads,
    .get_writes = CxxFramePass::_cb_get_writes,
    .get_inplace_aliases = CxxFramePass::_cb_get_inplace_aliases,
    .get_resource_specs = CxxFramePass::_cb_get_resource_specs,
    .get_internal_symbols = CxxFramePass::_cb_get_internal_symbols,
    .destroy = CxxFramePass::_cb_destroy,
    .serialize = nullptr,
    .deserialize = nullptr,
};

CxxFramePass::CxxFramePass() {
    _init_tc_pass();
}

CxxFramePass::~CxxFramePass() {
    _cleanup_tc_pass();
}

void CxxFramePass::_init_tc_pass() {
    tc_pass_init_unowned(&_c, &_cpp_vtable);
    _c.deleter = &CxxFramePass::delete_owned_pass;
    _c.kind = TC_NATIVE_PASS;
}

void CxxFramePass::_cleanup_tc_pass() {
    tc_pass_unlink_from_registry(&_c);
    if (_c.pass_name) {
        free(_c.pass_name);
        _c.pass_name = nullptr;
    }
    if (_c.viewport_name) {
        free(_c.viewport_name);
        _c.viewport_name = nullptr;
    }
    if (_c.debug_internal_symbol) {
        free(_c.debug_internal_symbol);
        _c.debug_internal_symbol = nullptr;
    }
}

} // namespace termin
