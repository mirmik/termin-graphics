#pragma once

#include <set>
#include <string>

#include <termin/render/frame_pass.hpp>

namespace termin {

class RENDER_API FrameDebugCapturePass : public CxxFramePass {
public:
    std::string source_resource;
    std::string source_type;
    bool paused = false;

private:
    FrameGraphCapture* capture_ = nullptr;
    FrameGraphCapture* depth_capture_ = nullptr;

public:
    explicit FrameDebugCapturePass(const std::string& pass_name = "FrameDebugger");
    static void register_type();

    void set_source_resource(const std::string& resource);
    void set_source_type(const std::string& type);
    void set_paused(bool value);
    void set_capture(FrameGraphCapture* capture);
    void set_depth_capture(FrameGraphCapture* capture);

    std::set<const char*> compute_reads() const override;
    std::set<const char*> compute_writes() const override;
    void execute(ExecuteContext& ctx) override;

private:
    bool source_is_depth() const;
};

} // namespace termin
