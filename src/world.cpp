#include <ecs/world.hpp>

#include <algorithm>
#include <cstring>

namespace ecs {

World::World() { findOrCreate({}); }  // arquétipo raiz: id == 0 (R-AR-3)

World::~World() = default;

Archetype* World::findOrCreate(Signature sig) {
    if (auto it = bySignature_.find(sig); it != bySignature_.end()) return it->second;
    const auto id = static_cast<ArchetypeId>(archetypes_.size());
    archetypes_.push_back(std::make_unique<Archetype>(id, sig));
    Archetype* a = archetypes_.back().get();
    bySignature_.emplace(std::move(sig), a);
    for (ComponentId cid : a->signature) {
        if (cid >= componentIndex_.size()) componentIndex_.resize(size_t(cid) + 1);
        componentIndex_[cid].insert(id, columnOf(*a, cid));
    }
    return a;
}

Entity World::place(Archetype& a) {
    Entity e = store_.allocate();
    EntityRecord& rec = store_.record(e.index);
    rec.row = a.table.pushUninit(e);
    rec.arch = &a;
    return e;
}

Entity World::create() { return place(*archetypes_[0]); }

void World::destroy(Entity e) {
    ECS_ASSERT(isAlive(e), "destroy e=%u:%u", e.index, e.generation);
    EntityRecord& rec = store_.record(e.index);
    Table& t = rec.arch->table;
    const uint32_t row = rec.row;
    t.destroyRow(row);
    if (auto moved = t.fillHole(row)) store_.record(moved->index).row = row;
    store_.release(e.index);
}

uint32_t World::moveEntity(Entity e, EntityRecord& rec, Archetype& dst, const Edge& edge) {
    Archetype& src = *rec.arch;
    const uint32_t srcRow = rec.row;
    const uint32_t dstRow = dst.table.pushUninit(e);
    for (uint16_t c = 0; c < src.table.columnCount(); ++c) {
        Column& sc = src.table.column(c);
        const int16_t d = edge.srcToDst[c];
        if (d >= 0)
            relocateValue(*sc.info, dst.table.column(static_cast<uint16_t>(d)).at(dstRow), sc.at(srcRow));
        else
            destroyValue(*sc.info, sc.at(srcRow));
    }
    if (auto moved = src.table.fillHole(srcRow)) store_.record(moved->index).row = srcRow;
    rec.arch = &dst;
    rec.row = dstRow;
    return dstRow;
}

void World::addRaw(Entity e, ComponentId cid, void* value) {
    const TypeInfo& ti = typeInfo(cid);
    ECS_ASSERT(isAlive(e) && iterating_ == 0, "add e=%u:%u type=%.*s", e.index, e.generation, int(ti.name.size()),
               ti.name.data());
    EntityRecord& rec = store_.record(e.index);
    Archetype& src = *rec.arch;
    if (std::binary_search(src.signature.begin(), src.signature.end(), cid)) {
        if (ti.tag) return;
        void* p = src.table.column(columnOf(src, cid)).at(rec.row);
        destroyValue(ti, p);
        if (ti.trivial)
            std::memcpy(p, value, ti.size);
        else
            ti.moveConstruct(p, value);
        return;
    }
    Signature sig = src.signature;
    sig.insert(std::lower_bound(sig.begin(), sig.end(), cid), cid);
    Archetype& dst = *findOrCreate(std::move(sig));
    Edge edge = buildEdge(src, dst, cid);
    const uint32_t dstRow = moveEntity(e, rec, dst, edge);
    if (edge.addedColumn >= 0) {
        void* p = dst.table.column(static_cast<uint16_t>(edge.addedColumn)).at(dstRow);
        if (ti.trivial)
            std::memcpy(p, value, ti.size);
        else
            ti.moveConstruct(p, value);
    }
}

void World::removeRaw(Entity e, ComponentId cid) {
    ECS_ASSERT(isAlive(e) && iterating_ == 0, "remove e=%u:%u type=%.*s", e.index, e.generation,
               int(typeInfo(cid).name.size()), typeInfo(cid).name.data());
    EntityRecord& rec = store_.record(e.index);
    Archetype& src = *rec.arch;
    auto it = std::lower_bound(src.signature.begin(), src.signature.end(), cid);
    if (it == src.signature.end() || *it != cid) return;
    Signature sig = src.signature;
    sig.erase(sig.begin() + (it - src.signature.begin()));
    Archetype& dst = *findOrCreate(std::move(sig));
    Edge edge = buildEdge(src, dst, NoComponent);
    moveEntity(e, rec, dst, edge);
}

}  // namespace ecs
