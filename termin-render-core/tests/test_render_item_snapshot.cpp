#include <cassert>
#include <memory>
#include <type_traits>

#include <termin/render/render_item_source.hpp>

namespace {

    class SnapshotTestSource final : public termin::RenderItemSource {
    private:
        tc_material_phase phase_{};

    protected:
        const char* source_name() const noexcept override {
            return "SnapshotTestSource";
        }

        bool collect_items(const termin::RenderItemSourceRequest&,
                           termin::RenderItemCollection& collection,
                           termin::RenderItemSnapshotCounters& counters) override {
            tc_render_item item{};
            item.kind = TC_RENDER_ITEM_KIND_MESH;
            item.source.domain_id = 77;
            item.source.namespace_id = 12;
            item.source.object_id = 34;
            item.source.generation = 5;
            item.source.subobject_id = 6;
            auto payload = std::make_shared<const int>(42);
            last_payload = payload;
            item.source.adapter_data =
                reinterpret_cast<uintptr_t>(collection.retain_adapter_payload(std::move(payload)));
            item.material_phase = &phase_;
            collection.items.push_back(item);

            counters.source_traversals = 1;
            counters.producers = 1;
            return true;
        }

    public:
        std::weak_ptr<const int> last_payload;

        SnapshotTestSource() {
            phase_.phase = TC_PHASE_EDITOR_DEBUG;
        }
    };

} // namespace

int main() {
    static_assert(std::is_standard_layout_v<tc_render_item_source>);

    SnapshotTestSource source;
    termin::RenderItemSnapshot snapshot;
    assert(source.publish(snapshot, {}));

    assert(snapshot.valid());
    assert(snapshot.item_count() == 1);
    assert(snapshot.item(0)->source.domain_id == 77);
    assert(snapshot.item(0)->source.generation == 5);
    const auto* payload = reinterpret_cast<const int*>(snapshot.item(0)->source.adapter_data);
    assert(payload != nullptr);
    assert(*payload == 42);
    assert(!source.last_payload.expired());
    assert(snapshot.storage().adapter_payloads.size() == 1);
    const size_t adapter_payload_capacity = snapshot.storage().adapter_payloads.capacity();
    const auto routed = snapshot.phase_item_indices(TC_PHASE_EDITOR_DEBUG);
    assert(routed.size() == 1);
    assert(routed[0] == 0);

    snapshot.invalidate_keep_capacity();
    assert(!snapshot.valid());
    assert(snapshot.item_count() == 0);
    assert(source.last_payload.expired());
    assert(snapshot.storage().adapter_payloads.empty());
    assert(snapshot.storage().adapter_payloads.capacity() == adapter_payload_capacity);
    return 0;
}
