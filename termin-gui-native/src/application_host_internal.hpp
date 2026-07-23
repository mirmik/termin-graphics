#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace tgfx {
class GraphicsHost;
}

namespace termin::gui_native {

class Document;
class DynamicTextureRecord;

struct GuiApplicationHostLeaseState {
    std::mutex mutex;
    std::thread::id owner_thread;
    std::function<void()> request_repaint;
    std::function<void(std::function<void()>)> defer;
    tgfx::GraphicsHost* graphics = nullptr;
    Document* document = nullptr;
    bool open = true;
    std::vector<std::shared_ptr<DynamicTextureRecord>> records;

    void register_record(const std::shared_ptr<DynamicTextureRecord>& record);
    void unregister_record(const DynamicTextureRecord* record);
    void close_all() noexcept;
};

} // namespace termin::gui_native
