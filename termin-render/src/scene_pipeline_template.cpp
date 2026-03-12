// scene_pipeline_template.cpp - C++ wrapper implementation
#include "termin/render/scene_pipeline_template.hpp"
#include <tcbase/tc_log.hpp>
#include <trent/json.h>
#include "termin/render/graph_compiler.hpp"
#include "termin/render/render_pipeline.hpp"
#include "tc_value_trent.hpp"

namespace termin {

TcScenePipelineTemplate TcScenePipelineTemplate::declare(
    const std::string& uuid,
    const std::string& name
) {
    tc_spt_handle h = tc_spt_declare(uuid.c_str(), name.c_str());
    return TcScenePipelineTemplate(h);
}

TcScenePipelineTemplate TcScenePipelineTemplate::find_by_uuid(const std::string& uuid) {
    tc_spt_handle h = tc_spt_find_by_uuid(uuid.c_str());
    return TcScenePipelineTemplate(h);
}

TcScenePipelineTemplate TcScenePipelineTemplate::find_by_name(const std::string& name) {
    tc_spt_handle h = tc_spt_find_by_name(name.c_str());
    return TcScenePipelineTemplate(h);
}

bool TcScenePipelineTemplate::is_valid() const {
    return tc_spt_is_valid(handle_);
}

bool TcScenePipelineTemplate::is_loaded() const {
    return tc_spt_is_loaded(handle_);
}

std::string TcScenePipelineTemplate::uuid() const {
    const char* s = tc_spt_get_uuid(handle_);
    return s ? s : "";
}

std::string TcScenePipelineTemplate::name() const {
    const char* s = tc_spt_get_name(handle_);
    return s ? s : "";
}

void TcScenePipelineTemplate::set_name(const std::string& name) {
    tc_spt_set_name(handle_, name.c_str());
}

void TcScenePipelineTemplate::set_from_json(const std::string& json) {
    try {
        nos::trent t = nos::json::parse(json);
        tc_value v = tc::trent_to_tc_value(t);
        tc_spt_set_graph(handle_, v);
    } catch (const std::exception& e) {
        tc::Log::error("[TcScenePipelineTemplate] Failed to parse JSON: %s", e.what());
    }
}

std::string TcScenePipelineTemplate::to_json() const {
    const tc_value* v = tc_spt_get_graph(handle_);
    if (!v || v->type == TC_VALUE_NIL) {
        return "{}";
    }

    try {
        nos::trent t = tc::tc_value_to_trent(*v);
        return nos::json::dump(t);
    } catch (const std::exception& e) {
        tc::Log::error("[TcScenePipelineTemplate] Failed to dump JSON: %s", e.what());
        return "{}";
    }
}

void TcScenePipelineTemplate::set_graph(tc_value graph) {
    tc_spt_set_graph(handle_, graph);
}

const tc_value* TcScenePipelineTemplate::get_graph() const {
    return tc_spt_get_graph(handle_);
}

std::vector<std::string> TcScenePipelineTemplate::target_viewports() const {
    std::vector<std::string> result;
    size_t count = tc_spt_viewport_count(handle_);
    result.reserve(count);

    for (size_t i = 0; i < count; i++) {
        const char* vp = tc_spt_get_viewport(handle_, i);
        if (vp) {
            result.push_back(vp);
        }
    }

    return result;
}

RenderPipeline* TcScenePipelineTemplate::compile() {
    if (!is_valid()) {
        tc::Log::error("[TcScenePipelineTemplate] Cannot compile: invalid handle");
        return nullptr;
    }

    if (!is_loaded()) {
        if (!ensure_loaded()) {
            tc::Log::error("[TcScenePipelineTemplate] Cannot compile: failed to load");
            return nullptr;
        }
    }

    const tc_value* v = tc_spt_get_graph(handle_);
    if (!v || v->type != TC_VALUE_DICT) {
        tc::Log::error("[TcScenePipelineTemplate] Cannot compile: no graph data");
        return nullptr;
    }

    try {
        nos::trent t = tc::tc_value_to_trent(*v);
        RenderPipeline* pipeline = tc::compile_graph(t);

        if (pipeline) {
            std::string n = name();
            if (!n.empty()) {
                pipeline->set_name(n);
            }
        }

        return pipeline;
    } catch (const std::exception& e) {
        tc::Log::error("[TcScenePipelineTemplate] Compile failed: %s", e.what());
        return nullptr;
    }
}

bool TcScenePipelineTemplate::ensure_loaded() {
    return tc_spt_ensure_loaded(handle_);
}

} // namespace termin
