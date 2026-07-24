#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include <termin/gui_native/tc_document.hpp>

namespace tgfx {
class GraphicsHost;
}

namespace termin::gui_native {

class DynamicTextureRecord;

struct GuiApplicationHostLeaseState {
    std::mutex mutex;
    std::function<void()> request_repaint;
    tgfx::GraphicsHost* graphics = nullptr;
    TcDocument document;
    bool open = true;
    std::vector<std::shared_ptr<DynamicTextureRecord>> records;

    void register_record(const std::shared_ptr<DynamicTextureRecord>& record);
    void unregister_record(const DynamicTextureRecord* record);
    void close_all() noexcept;
};

} // namespace termin::gui_native
