#pragma once

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
class GuiApplicationHost;

struct GuiApplicationHostLeaseState {
    std::mutex mutex;
    std::thread::id owner_thread;
    GuiApplicationHost* host = nullptr;
    tgfx::GraphicsHost* graphics = nullptr;
    Document* document = nullptr;
    bool open = true;
    std::vector<std::shared_ptr<DynamicTextureRecord>> records;

    void register_record(const std::shared_ptr<DynamicTextureRecord>& record);
    void unregister_record(const DynamicTextureRecord* record);
    void close_all() noexcept;
};

} // namespace termin::gui_native
