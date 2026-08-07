#include <cassert>
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

    bool collect_items(
        const termin::RenderItemSourceRequest&,
        termin::RenderItemCollection& collection,
        termin::RenderItemSnapshotCounters& counters) override
    {
        tc_render_item item{};
        item.kind = TC_RENDER_ITEM_KIND_MESH;
        item.source.domain_id = 77;
        item.source.namespace_id = 12;
        item.source.object_id = 34;
        item.source.generation = 5;
        item.source.subobject_id = 6;
        item.material_phase = &phase_;
        collection.items.push_back(item);

        counters.source_traversals = 1;
        counters.producers = 1;
        return true;
    }

public:
    SnapshotTestSource() {
        phase_.phase = TC_PHASE_EDITOR_DEBUG;
    }
};

} // namespace

int main()
{
    static_assert(std::is_standard_layout_v<tc_render_item_source>);

    SnapshotTestSource source;
    termin::RenderItemSnapshot snapshot;
    assert(source.publish(snapshot, {}));

    assert(snapshot.valid());
    assert(snapshot.item_count() == 1);
    assert(snapshot.item(0)->source.domain_id == 77);
    assert(snapshot.item(0)->source.generation == 5);
    const auto routed = snapshot.phase_item_indices(TC_PHASE_EDITOR_DEBUG);
    assert(routed.size() == 1);
    assert(routed[0] == 0);

    snapshot.invalidate_keep_capacity();
    assert(!snapshot.valid());
    assert(snapshot.item_count() == 0);
    return 0;
}
