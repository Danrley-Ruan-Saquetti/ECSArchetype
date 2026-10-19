#include "ecs/world.hpp"

#include <algorithm>
#include <cstring>

namespace ecs {
  namespace {
    bool checkEdgeMapping(const Archetype& a, const Archetype& b, const Edge& edge) {
      if (edge.srcToDst.size() != a.columnTypes.size())
        return false;

      for (size_t i = 0; i < a.columnTypes.size(); ++i) {
        const uint16_t col = columnOf(b, a.columnTypes[i]);
        const int16_t want = col == NoColumn ? int16_t(-1) : static_cast<int16_t>(col);
        if (edge.srcToDst[i] != want)
          return false;
      }

      return true;
    }

    bool fail(std::string* why, const char* id, const std::string& what) {
      if (why)
        *why = std::string(id) + " " + what;
      return false;
    }

    std::string entityLabel(uint32_t index, uint32_t generation) {
      return "e=" + std::to_string(index) + ":" + std::to_string(generation);
    }

    std::string archLabel(const Archetype& a) {
      return "arch=" + std::to_string(a.id);
    }

    bool checkTableShape(const Archetype& a, std::string* why) {
      const Table& t = a.table;
      if (t.entities().size() != t.count() || t.capacity() < t.count())
        return fail(why, "I-1", archLabel(a));

      return true;
    }

    bool checkColumnTypes(const Archetype& a, std::string* why) {
      if (!std::is_sorted(a.signature.begin(), a.signature.end()) || std::adjacent_find(a.signature.begin(), a.signature.end()) != a.signature.end())
        return fail(why, "I-5", archLabel(a) + " signature");

      std::vector<ComponentId> data;
      for (ComponentId c : a.signature)
        if (!typeInfo(c).tag)
          data.push_back(c);
      if (data != a.columnTypes)
        return fail(why, "I-5", archLabel(a) + " columnTypes");

      const Table& t = a.table;
      if (t.columnCount() != a.columnTypes.size() || !std::equal(t.columnTypes().begin(), t.columnTypes().end(), a.columnTypes.begin(), a.columnTypes.end()))
        return fail(why, "I-5", archLabel(a) + " table");

      return true;
    }

    Signature edgeSignature(Signature sig, ComponentId c, bool adding) {
      auto pos = std::lower_bound(sig.begin(), sig.end(), c);
      if (adding)
        sig.insert(pos, c);
      else if (pos != sig.end() && *pos == c)
        sig.erase(pos);
      return sig;
    }

    bool checkCachedEdges(const Archetype& a, std::string* why) {
      for (int adding = 1; adding >= 0; --adding) {
        for (const auto& [c, edge] : adding ? a.addEdges : a.removeEdges) {
          const std::string where = std::string(adding ? "add" : "remove") + " cid=" + std::to_string(c) + " " + archLabel(a);
          if (!edge.target)
            return fail(why, "I-9", where + " sem destino");

          const Archetype& b = *edge.target;
          if (b.signature != edgeSignature(a.signature, c, adding))
            return fail(why, "I-9", where + " assinatura");
          if (!checkEdgeMapping(a, b, edge))
            return fail(why, "I-9", where + " srcToDst");

          const int16_t wantAdded = adding && !typeInfo(c).tag ? static_cast<int16_t>(columnOf(b, c)) : int16_t(-1);
          if (edge.addedColumn != wantAdded)
            return fail(why, "I-9", where + " addedColumn");
        }
      }

      return true;
    }

    // Memo do último resultado do flush: entidades de um lote costumam ter o mesmo (origem, ops),
    // então a assinatura final, a aresta e as colunas são calculadas uma vez só.
    struct FlushMemo {
      bool valid = false;
      const Archetype* src = nullptr;
      std::vector<std::pair<ComponentId, bool>> key;
      Archetype* dst = nullptr;
      const Edge* edge = nullptr; // nullptr se src == dst ou entidade reservada
      std::vector<Column*> col; // por op: coluna no destino (nullptr: sem valor a construir)
      std::vector<uint8_t> existed; // por op: o componente já existia na origem

      bool matches(const Archetype* s, std::span<const NetComponentOp> ops) const {
        if (!valid || src != s || key.size() != ops.size())
          return false;
        for (size_t i = 0; i < ops.size(); ++i)
          if (key[i].first != ops[i].cid || key[i].second != ops[i].present)
            return false;

        return true;
      }

      void assign(const Archetype* s, std::span<const NetComponentOp> ops, Archetype* d, const Edge* e) {
        src = s;
        dst = d;
        edge = e;

        key.clear();
        col.clear();
        existed.clear();
        for (const NetComponentOp& op : ops) {
          key.emplace_back(op.cid, op.present);
          col.push_back(op.present && !typeInfo(op.cid).tag ? &dst->table.column(columnOf(*dst, op.cid)) : nullptr);
          existed.push_back(src && std::binary_search(src->signature.begin(), src->signature.end(), op.cid));
        }

        valid = true;
      }
    };

    Signature applyOps(Signature sig, std::span<const NetComponentOp> ops) {
      for (const NetComponentOp& op : ops) {
        auto pos = std::lower_bound(sig.begin(), sig.end(), op.cid);
        const bool has = pos != sig.end() && *pos == op.cid;
        if (op.present && !has)
          sig.insert(pos, op.cid);
        else if (!op.present && has)
          sig.erase(pos);
      }
      return sig;
    }

    void writeValues(const FlushMemo& memo, std::span<const NetComponentOp> ops, uint32_t row) {
      for (size_t i = 0; i < ops.size(); ++i) {
        if (!memo.col[i])
          continue;

        Column& c = *memo.col[i];
        void* p = c.at(row);
        if (memo.existed[i])
          destroyValue(*c.info, p);

        if (c.info->trivial) {
          // tamanhos pequenos e comuns inline: evita a chamada a memcpy de tamanho variável
          switch (c.info->size) {
          case 4: std::memcpy(p, ops[i].payload, 4); break;
          case 8: std::memcpy(p, ops[i].payload, 8); break;
          case 12: std::memcpy(p, ops[i].payload, 12); break;
          case 16: std::memcpy(p, ops[i].payload, 16); break;
          default: std::memcpy(p, ops[i].payload, c.info->size);
          }
        } else {
          c.info->moveConstruct(p, ops[i].payload);
        }
      }
    }
  }

  World::World() {
    findOrCreate({}); // o primeiro arquétipo recebe id 0: é o que root() devolve (R-AR-3)
  }

  World::~World() = default;

  Archetype* World::findOrCreate(Signature sig) {
    ++sigLookups_;
    if (auto it = bySignature_.find(sig); it != bySignature_.end())
      return it->second;

    const auto id = static_cast<ArchetypeId>(archetypes_.size());
    archetypes_.push_back(std::make_unique<Archetype>(id, sig));
    Archetype* a = archetypes_.back().get();
    bySignature_.emplace(std::move(sig), a);

    for (ComponentId cid : a->signature) {
      if (cid >= componentIndex_.size())
        componentIndex_.resize(size_t(cid) + 1);
      componentIndex_[cid].insert(id, columnOf(*a, cid));
    }
    for (QueryCacheBase* q : queries_)
      q->onArchetypeCreated(*a);

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
    return place(root());
  }

  void World::destroy(Entity e) {
    ECS_ASSERT(isAlive(e) && iterating_ == 0, "destroy e=%u:%u durante iteração=%u", e.index, e.generation, iterating_);

    EntityRecord& rec = store_.record(e.index);
    Table& t = rec.arch->table;
    const uint32_t row = rec.row;
    t.destroyRow(row);
    if (auto moved = t.fillHole(row))
      store_.record(moved->index).row = row;

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

    if (auto moved = src.table.fillHole(srcRow))
      store_.record(moved->index).row = srcRow;

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
      if (ti.tag)
        return;

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
    if (!std::binary_search(src.signature.begin(), src.signature.end(), cid))
      return;

    const Edge& edge = edgeFor(src, cid, false);
    moveEntity(e, rec, *edge.target, edge);
  }

  const Edge& World::edgeFor(Archetype& src, ComponentId cid, bool adding) {
    SmallMap<ComponentId, Edge>& edges = adding ? src.addEdges : src.removeEdges;
    if (const Edge* cached = edges.find(cid))
      return *cached;

    Signature sig = src.signature;
    auto pos = std::lower_bound(sig.begin(), sig.end(), cid);
    if (adding)
      sig.insert(pos, cid);
    else
      sig.erase(pos);

    Archetype* dst = findOrCreate(std::move(sig));
    return edges.insert(cid, buildEdge(src, *dst, adding ? cid : NoComponent));
  }

  bool World::checkInvariants(std::string* why) const {
    if (iterating_ != 0)
      return fail(why, "I-11", "iterating=" + std::to_string(iterating_));

    return checkArchetypes(why) && checkRecords(why) && checkFreeList(why) && checkComponentIndex(why) && checkQueries(why)
      && checkEdges(why);
  }

  bool World::checkArchetypes(std::string* why) const {
    for (size_t i = 0; i < archetypes_.size(); ++i) {
      const Archetype& a = *archetypes_[i];

      auto it = bySignature_.find(a.signature);
      if (a.id != i || it == bySignature_.end() || it->second != &a)
        return fail(why, "I-6", archLabel(a));

      if (!checkTableShape(a, why) || !checkColumnTypes(a, why) || !checkRows(a, why))
        return false;
    }

    if (bySignature_.size() != archetypes_.size())
      return fail(why, "I-6", "tamanhos diferentes");

    return true;
  }

  bool World::checkRows(const Archetype& a, std::string* why) const {
    const Table& t = a.table;
    for (uint32_t r = 0; r < t.count(); ++r) {
      Entity e = t.entities()[r];
      if (e.index >= store_.slotCount())
        return fail(why, "I-4", archLabel(a) + " row=" + std::to_string(r));

      const EntityRecord& rec = store_.record(e.index);
      if (rec.arch != &a || rec.row != r || rec.generation != e.generation)
        return fail(why, "I-4", entityLabel(e.index, e.generation) + " " + archLabel(a) + " row=" + std::to_string(r));
    }

    return true;
  }

  bool World::checkRecords(std::string* why) const {
    for (uint32_t i = 0; i < store_.slotCount(); ++i) {
      const EntityRecord& rec = store_.record(i);
      if (!rec.arch)
        continue;

      const Table& t = rec.arch->table;
      if (rec.row >= t.count() || t.entities()[rec.row] != Entity{i, rec.generation})
        return fail(why, "I-3", entityLabel(i, rec.generation));
    }

    return true;
  }

  bool World::checkFreeList(std::string* why) const {
    std::vector<uint32_t> fl(store_.freeList().begin(), store_.freeList().end());
    for (uint32_t i : fl)
      if (i >= store_.slotCount() || store_.record(i).arch != nullptr)
        return fail(why, "I-10", "slot=" + std::to_string(i));

    std::sort(fl.begin(), fl.end());
    if (std::adjacent_find(fl.begin(), fl.end()) != fl.end())
      return fail(why, "I-10", "repetido");

    return true;
  }

  bool World::checkComponentIndex(std::string* why) const {
    for (const auto& ap : archetypes_) {
      for (ComponentId c : ap->signature) {
        const uint16_t* col = c < componentIndex_.size() ? componentIndex_[c].find(ap->id) : nullptr;
        if (!col || *col != columnOf(*ap, c))
          return fail(why, "I-7", "cid=" + std::to_string(c) + " " + archLabel(*ap));
      }
    }

    for (size_t c = 0; c < componentIndex_.size(); ++c) {
      for (const auto& [aid, col] : componentIndex_[c]) {
        if (aid >= archetypes_.size())
          return fail(why, "I-7", "cid=" + std::to_string(c));

        const Archetype& a = *archetypes_[aid];
        if (!std::binary_search(a.signature.begin(), a.signature.end(), ComponentId(c)) || col != columnOf(a, ComponentId(c)))
          return fail(why, "I-7", "cid=" + std::to_string(c) + " " + archLabel(a));
      }
    }

    return true;
  }

  bool World::checkQueries(std::string* why) const {
    for (size_t qi = 0; qi < queries_.size(); ++qi)
      for (const auto& ap : archetypes_)
        if (queries_[qi]->isMatched(ap.get()) != queries_[qi]->matches(*ap))
          return fail(why, "I-8", "query=" + std::to_string(qi) + " " + archLabel(*ap));

    return true;
  }

  bool World::checkEdges(std::string* why) const {
    for (const auto& ap : archetypes_)
      if (!checkCachedEdges(*ap, why))
        return false;

    for (const auto& [key, edge] : jumpEdges_) {
      const uint32_t sid = static_cast<uint32_t>(key >> 32), did = static_cast<uint32_t>(key);
      const std::string where = "jump " + std::to_string(sid) + "->" + std::to_string(did);
      if (sid >= archetypes_.size() || did >= archetypes_.size())
        return fail(why, "I-9", where + " id");
      if (edge.target != archetypes_[did].get())
        return fail(why, "I-9", where + " destino");
      if (!checkEdgeMapping(*archetypes_[sid], *archetypes_[did], edge))
        return fail(why, "I-9", where + " srcToDst");
      if (edge.addedColumn != -1)
        return fail(why, "I-9", where + " addedColumn");
    }

    return true;
  }

  QueryCacheBase* World::registerQuery(QueryKey key, std::unique_ptr<QueryCacheBase> q) {
    QueryCacheBase* raw = q.get();
    queries_.push_back(raw);
    queryCache_.emplace(std::move(key), std::move(q));
    return raw;
  }

  // Índice do componente de `with` presente em menos arquétipos (R-QY-3); nullptr se `with` é vazio.
  const SmallMap<ArchetypeId, uint16_t>* World::smallestIndex(const Signature& with) const {
    static const SmallMap<ArchetypeId, uint16_t> empty;

    const SmallMap<ArchetypeId, uint16_t>* best = nullptr;
    for (ComponentId c : with) {
      const auto* idx = c < componentIndex_.size() ? &componentIndex_[c] : &empty;
      if (!best || idx->size() < best->size())
        best = idx;
    }

    return best;
  }

  void World::materialize(Entity reserved) {
    EntityRecord& rec = store_.record(reserved.index);
    Archetype& r = root();
    rec.row = r.table.pushUninit(reserved);
    rec.arch = &r;
  }

  void World::flushSequential(CommandBuffer& cb) {
    ECS_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    ECS_ASSERT(cb.store() == &store_, "flush de buffer criado por outro World");

    cb.forEach([&](const CommandView& v) {
      switch (v.op) {
      case CommandOp::Spawn:
        if (store_.isReserved(v.e))
          materialize(v.e);
        break;
      case CommandOp::Destroy:
        if (store_.isAlive(v.e))
          destroy(v.e);
        else if (store_.isReserved(v.e))
          store_.release(v.e.index);
        break;
      case CommandOp::Add:
        if (store_.isAlive(v.e))
          addRaw(v.e, v.cid, v.payload);
        break;
      case CommandOp::Remove:
        if (store_.isAlive(v.e))
          removeRaw(v.e, v.cid);
        break;
      }
    });

    cb.clear();
  }

  const Edge& World::jumpEdge(Archetype& src, Archetype& dst) {
    const uint64_t key = (uint64_t(src.id) << 32) | dst.id;
    if (auto it = jumpEdges_.find(key); it != jumpEdges_.end())
      return it->second;

    return jumpEdges_.emplace(key, buildEdge(src, dst, NoComponent)).first->second;
  }

  void World::flush(CommandBuffer& cb) {
    ECS_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    ECS_ASSERT(cb.store() == &store_, "flush de buffer criado por outro World");

    const CommandPlan plan = cb.plan();
    NetEffect net;
    FlushMemo memo;

    for (const EntityPlan& ep : plan.entities) {
      const Entity e = ep.e;
      cb.netEffect(plan, ep, net);

      const bool alive = store_.isAlive(e);
      const bool reserved = net.spawned && store_.isReserved(e);
      if (!alive && !reserved)
        continue; // handle obsoleto: ignorado em vez de cancelar os comandos no destroy (spec §10, questão 3)
      if (net.destroyed) {
        if (alive)
          destroy(e);
        else
          store_.release(e.index);
        continue;
      }

      const std::span<const NetComponentOp> ops = net.ops;
      EntityRecord& rec = store_.record(e.index);
      Archetype* src = alive ? rec.arch : nullptr;
      if (!memo.matches(src, ops)) {
        Archetype* dst = findOrCreate(applyOps(src ? src->signature : Signature{}, ops));
        memo.assign(src, ops, dst, (src && dst != src) ? &jumpEdge(*src, *dst) : nullptr);
      }

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

      writeValues(memo, ops, row);
    }

    cb.clear();
  }

  WorldStats World::stats() const {
    WorldStats st{};
    st.archetypeCount = archetypeCount();
    st.signatureLookups = sigLookups_;

    for (const auto& up : archetypes_) {
      const Archetype& a = *up;
      size_t rowBytes = sizeof(Entity);
      for (uint16_t i = 0; i < a.table.columnCount(); ++i)
        rowBytes += a.table.column(i).info->size;

      ArchetypeStats as{};
      as.id = a.id;
      as.signature = a.signature;
      as.count = a.table.count();
      as.capacity = a.table.capacity();
      as.edgeCount = static_cast<uint32_t>(a.addEdges.size() + a.removeEdges.size());
      as.columnBytes = size_t(as.capacity) * rowBytes;
      as.idleBytes = size_t(as.capacity - as.count) * rowBytes;
      as.signatureBytes = (a.signature.capacity() + a.columnTypes.capacity()) * sizeof(ComponentId);
      as.edgeBytes = a.addEdges.capacityBytes() + a.removeEdges.capacityBytes();
      for (const auto& [cid, e] : a.addEdges)
        as.edgeBytes += e.srcToDst.capacity() * sizeof(int16_t);
      for (const auto& [cid, e] : a.removeEdges)
        as.edgeBytes += e.srcToDst.capacity() * sizeof(int16_t);

      st.liveEntities += as.count;
      const uint32_t bucket = as.count == 0 ? 0 : 1 + (31 - static_cast<uint32_t>(__builtin_clz(as.count)));
      ++st.histogram[bucket];
      st.archetypes.push_back(std::move(as));
    }

    return st;
  }
}
