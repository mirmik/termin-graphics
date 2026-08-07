#include <cassert>
#include <type_traits>

#include <termin/render/render_item_snapshot.hpp>

int main()
{
    static_assert(std::is_standard_layout_v<tc_render_item_source>);

    tc_material_phase phase{};
    phase.phase = TC_PHASE_EDITOR_DEBUG;

    termin::RenderItemSnapshot snapshot;
    termin::RenderItemCollection& collection = snapshot.begin_collection();
    tc_render_item item{};
    item.kind = TC_RENDER_ITEM_KIND_MESH;
    item.source.domain_id = 77;
    item.source.namespace_id = 12;
    item.source.object_id = 34;
    item.source.generation = 5;
    item.source.subobject_id = 6;
    item.material_phase = &phase;
    collection.items.push_back(item);

    termin::RenderItemSnapshotCounters counters{};
    counters.source_traversals = 1;
    counters.producers = 1;
    snapshot.finish_collection(counters);

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
