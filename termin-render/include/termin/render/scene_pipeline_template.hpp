// scene_pipeline_template.hpp - C++ wrapper for tc_scene_pipeline_template
#pragma once

#include <string>
#include <vector>

extern "C" {
#include "core/tc_scene_pipeline_template.h"
}

namespace termin {

class RenderPipeline;

// C++ wrapper for tc_scene_pipeline_template
// Provides convenient access to graph data and compilation to RenderPipeline
class TcScenePipelineTemplate {
    tc_spt_handle handle_;

public:
    TcScenePipelineTemplate() : handle_(TC_SPT_HANDLE_INVALID) {}
    explicit TcScenePipelineTemplate(tc_spt_handle h) : handle_(h) {}

    // Factory methods
    static TcScenePipelineTemplate declare(const std::string& uuid, const std::string& name);
    static TcScenePipelineTemplate find_by_uuid(const std::string& uuid);
    static TcScenePipelineTemplate find_by_name(const std::string& name);

    // Validity checks
    bool is_valid() const;
    bool is_loaded() const;

    // Handle access
    tc_spt_handle handle() const { return handle_; }

    // UUID/name
    std::string uuid() const;
    std::string name() const;
    void set_name(const std::string& name);

    // Graph data - JSON interface
    void set_from_json(const std::string& json);
    std::string to_json() const;

    // Graph data - tc_value interface
    void set_graph(tc_value graph);
    const tc_value* get_graph() const;

    // Target viewports (extracted from graph_data.viewport_frames)
    std::vector<std::string> target_viewports() const;

    // Compile to RenderPipeline (caller owns result)
    RenderPipeline* compile();

    // Ensure data is loaded (triggers lazy load if needed)
    bool ensure_loaded();
};

} // namespace termin
