#include <termin/render/unknown_pass.hpp>

namespace termin {

UnknownPass::UnknownPass() {
    link_to_type_registry("UnknownPass");
}

UnknownPass::~UnknownPass() {
    tc_value_free(&original_data);
}

std::set<const char*> UnknownPass::compute_reads() const {
    std::set<const char*> result;
    for (const std::string& value : original_reads) result.insert(value.c_str());
    return result;
}

std::set<const char*> UnknownPass::compute_writes() const {
    std::set<const char*> result;
    for (const std::string& value : original_writes) result.insert(value.c_str());
    return result;
}

std::vector<std::pair<std::string, std::string>> UnknownPass::get_inplace_aliases() const {
    return original_inplace_aliases;
}

std::vector<ResourceSpec> UnknownPass::get_resource_specs() const {
    return original_resource_specs;
}

std::vector<std::string> UnknownPass::get_internal_symbols() const {
    return original_internal_symbols;
}

namespace {

tc_pass* create_unknown_pass(void*) {
    auto* pass = new UnknownPass();
    pass->retain();
    return pass->tc_pass_ptr();
}

struct UnknownPassBootstrapRegistration {
    UnknownPassBootstrapRegistration() { ensure_unknown_pass_registered(); }
};

UnknownPassBootstrapRegistration g_unknown_pass_bootstrap_registration;

} // namespace

void ensure_unknown_pass_registered() {
    if (tc_pass_registry_has("UnknownPass")) return;
    tc_pass_registry_register(
        "UnknownPass",
        create_unknown_pass,
        nullptr,
        TC_NATIVE_PASS
    );
    tc::InspectRegistry::instance().set_type_parent("UnknownPass", "CxxFramePass");
}

} // namespace termin
