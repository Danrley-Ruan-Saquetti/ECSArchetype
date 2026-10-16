#include <ecs/world.hpp>

#include <algorithm>
#include <cstring>

namespace ecs {

namespace {
// srcToDst[i] deve apontar para a coluna de b com o mesmo ComponentId (ou -1 sse ausente em b).
bool checkEdgeMapping(const Archetype& a, const Archetype& b, const Edge& edge) {
    if (edge.srcToDst.size() != a.columnTypes.size()) return false;
    for (size_t i = 0; i < a.columnTypes.size(); ++i) {
        const uint16_t col = columnOf(b, a.columnTypes[i]);
        const int16_t want = col == NoColumn ? int16_t(-1) : static_cast<int16_t>(col);
        if (edge.srcToDst[i] != want) return false;
    }
    return true;
}
}  // namespace

World::World() { findOrCreate({}); }  // arquétipo raiz: id == 0 (R-AR-3)

World::~World() = default;

Archetype* World::findOrCreate(Signature sig) {
    ++sigLookups_;
    if (auto it = bySignature_.find(sig); it != bySignature_.end()) return it->second;
    const auto id = static_cast<ArchetypeId>(archetypes_.size());
    archetypes_.push_back(std::make_unique<Archetype>(id, sig));
    Archetype* a = archetypes_.back().get();
    bySignature_.emplace(std::move(sig), a);
    for (ComponentId cid : a->signature) {
        if (cid >= componentIndex_.size()) componentIndex_.resize(size_t(cid) + 1);
        componentIndex_[cid].insert(id, columnOf(*a, cid));
    }
    for (QueryCacheBase* q : queries_) q->onArchetypeCreated(*a);
    return a;
}

Entity World::place(Archetype& a) {
    Entity e = store_.allocate();
    EntityRecord& rec = store_.record(e.index);
    rec.row = a.table.pushUninit(e);
    rec.arch = &a;
    return e;
}

Entity World::create() {
    ECS_ASSERT(iterating_ == 0, "create durante iteração=%u", iterating_);
    return place(*archetypes_[0]);
}

void World::destroy(Entity e) {
    ECS_ASSERT(isAlive(e) && iterating_ == 0, "destroy e=%u:%u durante iteração=%u", e.index, e.generation, iterating_);
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
    const Edge& edge = edgeFor(src, cid, true);
    Archetype& dst = *edge.target;
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
    if (!std::binary_search(src.signature.begin(), src.signature.end(), cid)) return;
    const Edge& edge = edgeFor(src, cid, false);
    moveEntity(e, rec, *edge.target, edge);
}

const Edge& World::edgeFor(Archetype& src, ComponentId cid, bool adding) {
    SmallMap<ComponentId, Edge>& edges = adding ? src.addEdges : src.removeEdges;
    if (const Edge* cached = edges.find(cid)) return *cached;
    Signature sig = src.signature;
    auto pos = std::lower_bound(sig.begin(), sig.end(), cid);
    if (adding)
        sig.insert(pos, cid);
    else
        sig.erase(pos);
    Archetype* dst = findOrCreate(std::move(sig));
    return edges.insert(cid, buildEdge(src, *dst, adding ? cid : NoComponent));
}

}  // namespace ecs

namespace ecs {

bool World::checkInvariants(std::string* why) const {
    auto fail = [&](const char* id, const std::string& what) {
        if (why) *why = std::string(id) + " " + what;
        return false;
    };
    auto ent = [](uint32_t i, uint32_t g) { return "e=" + std::to_string(i) + ":" + std::to_string(g); };
    auto arch = [](const Archetype& a) { return "arch=" + std::to_string(a.id); };

    // I-11
    if (iterating_ != 0) return fail("I-11", "iterating=" + std::to_string(iterating_));

    for (size_t i = 0; i < archetypes_.size(); ++i) {
        const Archetype& a = *archetypes_[i];
        const Table& t = a.table;
        // I-6 (metade): id == posição e lookup pela própria assinatura
        if (a.id != i) return fail("I-6", arch(a));
        auto it = bySignature_.find(a.signature);
        if (it == bySignature_.end() || it->second != &a) return fail("I-6", arch(a));

        // I-1
        if (t.entities().size() != t.count() || t.capacity() < t.count()) return fail("I-1", arch(a));

        // I-5
        if (!std::is_sorted(a.signature.begin(), a.signature.end()) ||
            std::adjacent_find(a.signature.begin(), a.signature.end()) != a.signature.end())
            return fail("I-5", arch(a) + " signature");
        std::vector<ComponentId> data;
        for (ComponentId c : a.signature)
            if (!typeInfo(c).tag) data.push_back(c);
        if (data != a.columnTypes) return fail("I-5", arch(a) + " columnTypes");
        if (t.columnCount() != a.columnTypes.size() ||
            !std::equal(t.columnTypes().begin(), t.columnTypes().end(), a.columnTypes.begin(), a.columnTypes.end()))
            return fail("I-5", arch(a) + " table");

        // I-4
        for (uint32_t r = 0; r < t.count(); ++r) {
            Entity e = t.entities()[r];
            if (e.index >= store_.slotCount()) return fail("I-4", arch(a) + " row=" + std::to_string(r));
            const EntityRecord& rec = store_.record(e.index);
            if (rec.arch != &a || rec.row != r || rec.generation != e.generation)
                return fail("I-4", ent(e.index, e.generation) + " " + arch(a) + " row=" + std::to_string(r));
        }
    }
    // I-6
    if (bySignature_.size() != archetypes_.size()) return fail("I-6", "tamanhos diferentes");

    // I-3 e I-10
    for (uint32_t i = 0; i < store_.slotCount(); ++i) {
        const EntityRecord& rec = store_.record(i);
        if (!rec.arch) continue;
        const Table& t = rec.arch->table;
        if (rec.row >= t.count() || t.entities()[rec.row] != Entity{i, rec.generation})
            return fail("I-3", ent(i, rec.generation));
    }
    {
        std::vector<uint32_t> fl(store_.freeList().begin(), store_.freeList().end());
        for (uint32_t i : fl)
            if (i >= store_.slotCount() || store_.record(i).arch != nullptr) return fail("I-10", "slot=" + std::to_string(i));
        std::sort(fl.begin(), fl.end());
        if (std::adjacent_find(fl.begin(), fl.end()) != fl.end()) return fail("I-10", "repetido");
    }

    // I-7
    for (const auto& ap : archetypes_) {
        for (ComponentId c : ap->signature) {
            const uint16_t* col = c < componentIndex_.size() ? componentIndex_[c].find(ap->id) : nullptr;
            if (!col || *col != columnOf(*ap, c)) return fail("I-7", "cid=" + std::to_string(c) + " " + arch(*ap));
        }
    }
    for (size_t c = 0; c < componentIndex_.size(); ++c) {
        for (const auto& [aid, col] : componentIndex_[c]) {
            if (aid >= archetypes_.size()) return fail("I-7", "cid=" + std::to_string(c));
            const Archetype& a = *archetypes_[aid];
            if (!std::binary_search(a.signature.begin(), a.signature.end(), ComponentId(c)) ||
                col != columnOf(a, ComponentId(c)))
                return fail("I-7", "cid=" + std::to_string(c) + " " + arch(a));
        }
    }
    // I-8
    for (size_t qi = 0; qi < queries_.size(); ++qi)
        for (const auto& ap : archetypes_)
            if (queries_[qi]->isMatched(ap.get()) != queries_[qi]->matches(*ap))
                return fail("I-8", "query=" + std::to_string(qi) + " " + arch(*ap));

    // I-9
    for (const auto& ap : archetypes_) {
        const Archetype& a = *ap;
        for (int adding = 1; adding >= 0; --adding) {
            for (const auto& [c, edge] : adding ? a.addEdges : a.removeEdges) {
                const std::string where = std::string(adding ? "add" : "remove") + " cid=" + std::to_string(c) + " " + arch(a);
                if (!edge.target) return fail("I-9", where + " sem destino");
                const Archetype& b = *edge.target;
                Signature expected = a.signature;
                auto pos = std::lower_bound(expected.begin(), expected.end(), c);
                if (adding)
                    expected.insert(pos, c);
                else if (pos != expected.end() && *pos == c)
                    expected.erase(pos);
                if (b.signature != expected) return fail("I-9", where + " assinatura");
                if (!checkEdgeMapping(a, b, edge)) return fail("I-9", where + " srcToDst");
                int16_t wantAdded = -1;
                if (adding && !typeInfo(c).tag) wantAdded = static_cast<int16_t>(columnOf(b, c));
                if (edge.addedColumn != wantAdded) return fail("I-9", where + " addedColumn");
            }
        }
    }
    // I-9 (arestas de salto do flush)
    for (const auto& [key, edge] : jumpEdges_) {
        const uint32_t sid = static_cast<uint32_t>(key >> 32), did = static_cast<uint32_t>(key);
        const std::string where = "jump " + std::to_string(sid) + "->" + std::to_string(did);
        if (sid >= archetypes_.size() || did >= archetypes_.size()) return fail("I-9", where + " id");
        if (edge.target != archetypes_[did].get()) return fail("I-9", where + " destino");
        if (!checkEdgeMapping(*archetypes_[sid], *archetypes_[did], edge)) return fail("I-9", where + " srcToDst");
        if (edge.addedColumn != -1) return fail("I-9", where + " addedColumn");
    }
    return true;
}

}  // namespace ecs

namespace ecs {

QueryCacheBase* World::registerQuery(QueryKey key, std::unique_ptr<QueryCacheBase> q) {
    QueryCacheBase* raw = q.get();
    queries_.push_back(raw);
    queryCache_.emplace(std::move(key), std::move(q));
    return raw;
}

// c* = componente de `with` presente em menos arquétipos (R-QY-3); nullptr se `with` é vazio.
const SmallMap<ArchetypeId, uint16_t>* World::smallestIndex(const Signature& with) const {
    static const SmallMap<ArchetypeId, uint16_t> empty;
    const SmallMap<ArchetypeId, uint16_t>* best = nullptr;
    for (ComponentId c : with) {
        const auto* idx = c < componentIndex_.size() ? &componentIndex_[c] : &empty;
        if (!best || idx->size() < best->size()) best = idx;
    }
    return best;
}

}  // namespace ecs

namespace ecs {

void World::materialize(Entity e) {
    EntityRecord& rec = store_.record(e.index);
    Archetype& root = *archetypes_[0];
    rec.row = root.table.pushUninit(e);
    rec.arch = &root;
}

void World::flushSequential(CommandBuffer& cb) {
    ECS_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    ECS_ASSERT(cb.store() == &store_, "flush de buffer criado por outro World");
    cb.forEach([&](const CommandView& v) {
        switch (v.op) {
            case CommandOp::Spawn:
                if (store_.isReserved(v.e)) materialize(v.e);
                break;
            case CommandOp::Destroy:
                if (store_.isAlive(v.e))
                    destroy(v.e);
                else if (store_.isReserved(v.e))
                    store_.release(v.e.index);
                break;
            case CommandOp::Add:
                if (store_.isAlive(v.e)) addRaw(v.e, v.cid, v.payload);
                break;
            case CommandOp::Remove:
                if (store_.isAlive(v.e)) removeRaw(v.e, v.cid);
                break;
        }
    });
    cb.clear();
}

}  // namespace ecs

namespace ecs {

const Edge& World::jumpEdge(Archetype& src, Archetype& dst) {
    const uint64_t key = (uint64_t(src.id) << 32) | dst.id;
    if (auto it = jumpEdges_.find(key); it != jumpEdges_.end()) return it->second;
    return jumpEdges_.emplace(key, buildEdge(src, dst, NoComponent)).first->second;
}

void World::flush(CommandBuffer& cb) {
    ECS_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    ECS_ASSERT(cb.store() == &store_, "flush de buffer criado por outro World");
    const CommandPlan plan = cb.plan();

    // Memo do último resultado: entidades de um lote costumam ter o mesmo (origem, ops),
    // então a assinatura final, a aresta e as colunas são calculadas uma vez só.
    struct Memo {
        bool valid = false;
        const Archetype* src = nullptr;
        std::vector<std::pair<ComponentId, bool>> key;
        Archetype* dst = nullptr;
        const Edge* edge = nullptr;  // nullptr se src == dst ou entidade reservada
        std::vector<int32_t> col;    // por op: coluna no destino (-1: sem valor a construir)
        std::vector<uint8_t> existed;  // por op: o componente já existia na origem
    } memo;
    auto memoMatches = [&](const Archetype* src, std::span<const NetComponentOp> ops) {
        if (!memo.valid || memo.src != src || memo.key.size() != ops.size()) return false;
        for (size_t i = 0; i < ops.size(); ++i)
            if (memo.key[i].first != ops[i].cid || memo.key[i].second != ops[i].present) return false;
        return true;
    };
    auto memoBuild = [&](Archetype* src, std::span<const NetComponentOp> ops) {
        Signature sig = src ? src->signature : Signature{};
        for (const NetComponentOp& op : ops) {
            auto pos = std::lower_bound(sig.begin(), sig.end(), op.cid);
            const bool has = pos != sig.end() && *pos == op.cid;
            if (op.present && !has)
                sig.insert(pos, op.cid);
            else if (!op.present && has)
                sig.erase(pos);
        }
        memo.dst = findOrCreate(std::move(sig));
        memo.edge = (src && memo.dst != src) ? &jumpEdge(*src, *memo.dst) : nullptr;
        memo.src = src;
        memo.key.clear();
        memo.col.clear();
        memo.existed.clear();
        for (const NetComponentOp& op : ops) {
            memo.key.emplace_back(op.cid, op.present);
            memo.col.push_back(op.present && !typeInfo(op.cid).tag ? int32_t(columnOf(*memo.dst, op.cid)) : -1);
            memo.existed.push_back(src && std::binary_search(src->signature.begin(), src->signature.end(), op.cid));
        }
        memo.valid = true;
    };

    for (const EntityPlan& ep : plan.entities) {
        const Entity e = ep.e;
        const bool alive = store_.isAlive(e);
        const bool reserved = ep.spawned && store_.isReserved(e);
        if (!alive && !reserved) continue;  // handle obsoleto: ignorado (questão 3)
        if (ep.destroyed) {
            if (alive)
                destroy(e);
            else
                store_.release(e.index);
            continue;
        }
        const std::span<const NetComponentOp> ops = plan.opsOf(ep);
        EntityRecord& rec = store_.record(e.index);
        Archetype* src = alive ? rec.arch : nullptr;
        if (!memoMatches(src, ops)) memoBuild(src, ops);

        uint32_t row;
        if (!src) {
            row = memo.dst->table.pushUninit(e);
            rec.arch = memo.dst;
            rec.row = row;
        } else if (memo.edge) {
            row = moveEntity(e, rec, *memo.dst, *memo.edge);
        } else {
            row = rec.row;
        }
        for (size_t i = 0; i < ops.size(); ++i) {
            if (memo.col[i] < 0) continue;
            Column& c = memo.dst->table.column(static_cast<uint16_t>(memo.col[i]));
            void* p = c.at(row);
            if (memo.existed[i]) destroyValue(*c.info, p);
            if (c.info->trivial)
                std::memcpy(p, ops[i].payload, c.info->size);
            else
                c.info->moveConstruct(p, ops[i].payload);
        }
    }
    cb.clear();
}

}  // namespace ecs
