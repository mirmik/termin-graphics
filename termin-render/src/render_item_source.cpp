#include <termin/render/render_item_source.hpp>

#include <tcbase/tc_log.hpp>

namespace termin {

    RenderItemSource::~RenderItemSource() = default;

    bool RenderItemSource::publish(RenderItemSnapshot& snapshot, const RenderItemSourceRequest& request) {
        const char* source = source_name();
        if (!source || source[0] == '\0') {
            source = "RenderItemSource";
        }
        const char* request_name = request.debug_name;
        if (!request_name || request_name[0] == '\0') {
            request_name = "unnamed request";
        }

        RenderItemCollection& output = snapshot.begin_collection();
        RenderItemSnapshotCounters counters{};
        if (!collect_items(request, output, counters)) {
            snapshot.invalidate_keep_capacity();
            tc::Log::error("[%s] failed to publish RenderItemSnapshot for %s", source, request_name);
            return false;
        }

        snapshot.finish_collection(counters);
        return true;
    }

} // namespace termin
