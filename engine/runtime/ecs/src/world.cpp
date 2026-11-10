#include "oly/ecs/world.hpp"

#include <algorithm>
#include <cstring>

namespace oly::ecs {
  namespace {
    bool checkEdgeMapping(const detail::Archetype& source, const detail::Archetype& destination, const detail::Edge& edge) {
      if (edge.sourceToDestination.size() != source.columnTypes.size()) {
        return false;
      }

      for (size_t i = 0; i < source.columnTypes.size(); ++i) {
        const uint16_t column = detail::columnOf(destination, source.columnTypes[i]);
        const int16_t expected = column == detail::NoColumn ? int16_t(-1) : static_cast<int16_t>(column);
        if (edge.sourceToDestination[i] != expected) {
          return false;
        }
      }

      return true;
    }

    bool fail(std::string* why, const char* invariantId, const std::string& what) {
      if (why) {
        *why = std::string(invariantId) + " " + what;
      }

      return false;
    }

    std::string entityLabel(uint32_t index, uint32_t generation) {
      return "e=" + std::to_string(index) + ":" + std::to_string(generation);
    }

    std::string archetypeLabel(const detail::Archetype& archetype) {
      return "arch=" + std::to_string(archetype.id);
    }

    bool checkTableShape(const detail::Archetype& archetype, std::string* why) {
      const detail::Table& table = archetype.table;
      if (table.entities().size() != table.count() || table.capacity() < table.count()) {
        return fail(why, "I-1", archetypeLabel(archetype));
      }

      return true;
    }

    bool checkColumnTypes(const detail::Archetype& archetype, std::string* why) {
      if (!std::is_sorted(archetype.signature.begin(), archetype.signature.end()) || std::adjacent_find(archetype.signature.begin(), archetype.signature.end()) != archetype.signature.end()) {
        return fail(why, "I-5", archetypeLabel(archetype) + " signature");
      }

      std::vector<ComponentId> data;
      for (ComponentId componentId : archetype.signature) {
        if (!typeInfo(componentId).tag) {
          data.push_back(componentId);
        }
      }

      if (data != archetype.columnTypes) {
        return fail(why, "I-5", archetypeLabel(archetype) + " columnTypes");
      }

      const detail::Table& table = archetype.table;
      if (table.columnCount() != archetype.columnTypes.size() || !std::equal(table.columnTypes().begin(), table.columnTypes().end(), archetype.columnTypes.begin(), archetype.columnTypes.end())) {
        return fail(why, "I-5", archetypeLabel(archetype) + " table");
      }

      return true;
    }

    detail::Signature edgeSignature(detail::Signature signature, ComponentId componentId, bool adding) {
      auto position = std::lower_bound(signature.begin(), signature.end(), componentId);
      if (adding) {
        signature.insert(position, componentId);
      } else if (position != signature.end() && *position == componentId) {
        signature.erase(position);
      }

      return signature;
    }

    bool checkCachedEdges(const detail::Archetype& archetype, std::string* why) {
      for (int adding = 1; adding >= 0; --adding) {
        for (const auto& [componentId, edge] : adding ? archetype.addEdges : archetype.removeEdges) {
          const std::string where = std::string(adding ? "add" : "remove") + " cid=" + std::to_string(componentId) + " " + archetypeLabel(archetype);
          if (!edge.target) {
            return fail(why, "I-9", where + " sem destino");
          }

          const detail::Archetype& target = *edge.target;
          if (target.signature != edgeSignature(archetype.signature, componentId, adding)) {
            return fail(why, "I-9", where + " assinatura");
          }
          if (!checkEdgeMapping(archetype, target, edge)) {
            return fail(why, "I-9", where + " sourceToDestination");
          }

          const int16_t expectedAddedColumn = adding && !typeInfo(componentId).tag ? static_cast<int16_t>(detail::columnOf(target, componentId)) : int16_t(-1);
          if (edge.addedColumn != expectedAddedColumn) {
            return fail(why, "I-9", where + " addedColumn");
          }
        }
      }

      return true;
    }

    // Memo do último resultado do flush: entidades de um lote costumam ter o mesmo (origem, operações),
    // então a assinatura final, a aresta e as colunas são calculadas uma vez só.
    struct FlushMemo {
      bool valid = false;
      const detail::Archetype* source = nullptr;
      std::vector<std::pair<ComponentId, bool>> key;
      detail::Archetype* destination = nullptr;
      const detail::Edge* edge = nullptr; // nullptr se source == destination ou entidade reservada
      std::vector<detail::Column*> columns; // por operação: coluna no destino (nullptr: sem valor a construir)
      std::vector<uint8_t> existed; // por operação: o componente já existia na origem

      bool matches(const detail::Archetype* candidateSource, std::span<const detail::NetComponentOperation> operations) const {
        if (!valid || source != candidateSource || key.size() != operations.size()) {
          return false;
        }

        for (size_t i = 0; i < operations.size(); ++i) {
          if (key[i].first != operations[i].componentId || key[i].second != operations[i].present) {
            return false;
          }
        }

        return true;
      }

      void assign(const detail::Archetype* newSource, std::span<const detail::NetComponentOperation> operations, detail::Archetype* newDestination, const detail::Edge* newEdge) {
        source = newSource;
        destination = newDestination;
        edge = newEdge;

        key.clear();
        columns.clear();
        existed.clear();
        for (const detail::NetComponentOperation& operation : operations) {
          key.emplace_back(operation.componentId, operation.present);
          columns.push_back(operation.present && !typeInfo(operation.componentId).tag ? &destination->table.column(detail::columnOf(*destination, operation.componentId)) : nullptr);
          existed.push_back(source && std::binary_search(source->signature.begin(), source->signature.end(), operation.componentId));
        }

        valid = true;
      }
    };

    detail::Signature applyOperations(detail::Signature signature, std::span<const detail::NetComponentOperation> operations) {
      for (const detail::NetComponentOperation& operation : operations) {
        auto position = std::lower_bound(signature.begin(), signature.end(), operation.componentId);
        const bool has = position != signature.end() && *position == operation.componentId;
        if (operation.present && !has) {
          signature.insert(position, operation.componentId);
        } else if (!operation.present && has) {
          signature.erase(position);
        }
      }

      return signature;
    }

    // `slot` não pode estar construído; `value` fica moved-from, e destruí-lo é do chamador.
    void constructValue(const TypeInfo& info, void* slot, void* value) noexcept {
      if (!info.trivial) {
        info.moveConstruct(slot, value);
        return;
      }

      // tamanhos pequenos e comuns inline: evita a chamada a memcpy de tamanho variável
      switch (info.size) {
      case 4: std::memcpy(slot, value, 4); break;
      case 8: std::memcpy(slot, value, 8); break;
      case 12: std::memcpy(slot, value, 12); break;
      case 16: std::memcpy(slot, value, 16); break;
      default: std::memcpy(slot, value, info.size);
      }
    }

    void writeValues(const FlushMemo& memo, std::span<const detail::NetComponentOperation> operations, uint32_t row) {
      for (size_t i = 0; i < operations.size(); ++i) {
        if (!memo.columns[i]) {
          continue;
        }

        detail::Column& column = *memo.columns[i];
        void* slot = column.at(row);
        if (memo.existed[i]) {
          detail::destroyValue(*column.info, slot);
        }

        constructValue(*column.info, slot, operations[i].payload);
      }
    }
  }

  World::World() {
    findOrCreate({}); // o primeiro arquétipo recebe id 0: é o que root() devolve (R-AR-3)
  }

  World::~World() = default;

  detail::Archetype* World::findOrCreate(detail::Signature signature) {
    ++signatureLookups_;

    if (auto it = bySignature_.find(signature); it != bySignature_.end()) {
      return it->second;
    }

    const auto id = static_cast<detail::ArchetypeId>(archetypes_.size());
    archetypes_.push_back(std::make_unique<detail::Archetype>(id, signature));
    detail::Archetype* archetype = archetypes_.back().get();
    bySignature_.emplace(std::move(signature), archetype);

    for (ComponentId componentId : archetype->signature) {
      if (componentId >= componentIndex_.size()) {
        componentIndex_.resize(size_t(componentId) + 1);
      }
      componentIndex_[componentId].insert(id, detail::columnOf(*archetype, componentId));
    }

    for (detail::QueryCacheBase* query : queries_) {
      query->onArchetypeCreated(*archetype);
    }

    return archetype;
  }

  Entity World::place(detail::Archetype& archetype) {
    Entity entity = store_.allocate();
    detail::EntityRecord& record = store_.record(entity.index);

    record.row = archetype.table.pushUninitialized(entity);
    record.archetype = &archetype;

    return entity;
  }

  Entity World::create() {
    OLY_ASSERT(iterating_ == 0, "create durante iteração=%u", iterating_);
    return place(root());
  }

  void World::destroy(Entity entity) {
    OLY_ASSERT(isAlive(entity) && iterating_ == 0, "destroy e=%u:%u durante iteração=%u", entity.index, entity.generation, iterating_);

    detail::EntityRecord& record = store_.record(entity.index);
    detail::Table& table = record.archetype->table;
    const uint32_t row = record.row;

    table.destroyRow(row);
    if (auto moved = table.fillHole(row)) {
      store_.record(moved->index).row = row;
    }

    store_.release(entity.index);
  }

  uint32_t World::moveEntity(Entity entity, detail::EntityRecord& record, detail::Archetype& destination, const detail::Edge& edge) {
    detail::Archetype& source = *record.archetype;
    const uint32_t sourceRow = record.row;
    const uint32_t destinationRow = destination.table.pushUninitialized(entity);

    for (uint16_t columnIndex = 0; columnIndex < source.table.columnCount(); ++columnIndex) {
      detail::Column& sourceColumn = source.table.column(columnIndex);
      const int16_t destinationColumn = edge.sourceToDestination[columnIndex];
      if (destinationColumn >= 0) {
        detail::relocateValue(*sourceColumn.info, destination.table.column(static_cast<uint16_t>(destinationColumn)).at(destinationRow), sourceColumn.at(sourceRow));
      } else {
        detail::destroyValue(*sourceColumn.info, sourceColumn.at(sourceRow));
      }
    }

    if (auto moved = source.table.fillHole(sourceRow)) {
      store_.record(moved->index).row = sourceRow;
    }

    record.archetype = &destination;
    record.row = destinationRow;

    return destinationRow;
  }

  void World::addRaw(Entity entity, ComponentId componentId, void* value) {
    const TypeInfo& info = typeInfo(componentId);
    OLY_ASSERT(isAlive(entity) && iterating_ == 0, "add e=%u:%u type=%.*s", entity.index, entity.generation, int(info.name.size()),
               info.name.data());

    detail::EntityRecord& record = store_.record(entity.index);
    detail::Archetype& source = *record.archetype;

    if (std::binary_search(source.signature.begin(), source.signature.end(), componentId)) {
      if (info.tag) {
        return;
      }

      void* slot = source.table.column(detail::columnOf(source, componentId)).at(record.row);
      detail::destroyValue(info, slot);
      constructValue(info, slot, value);
      return;
    }

    const detail::Edge& edge = edgeFor(source, componentId, true);
    detail::Archetype& destination = *edge.target;
    const uint32_t destinationRow = moveEntity(entity, record, destination, edge);

    if (edge.addedColumn >= 0) {
      void* slot = destination.table.column(static_cast<uint16_t>(edge.addedColumn)).at(destinationRow);
      constructValue(info, slot, value);
    }
  }

  void World::removeRaw(Entity entity, ComponentId componentId) {
    OLY_ASSERT(isAlive(entity) && iterating_ == 0, "remove e=%u:%u type=%.*s", entity.index, entity.generation,
               int(typeInfo(componentId).name.size()), typeInfo(componentId).name.data());

    detail::EntityRecord& record = store_.record(entity.index);
    detail::Archetype& source = *record.archetype;

    if (!std::binary_search(source.signature.begin(), source.signature.end(), componentId)) {
      return;
    }

    const detail::Edge& edge = edgeFor(source, componentId, false);
    moveEntity(entity, record, *edge.target, edge);
  }

  const detail::Edge& World::edgeFor(detail::Archetype& source, ComponentId componentId, bool adding) {
    core::SmallMap<ComponentId, detail::Edge>& edges = adding ? source.addEdges : source.removeEdges;
    if (const detail::Edge* cached = edges.find(componentId)) {
      return *cached;
    }

    detail::Signature signature = source.signature;
    auto position = std::lower_bound(signature.begin(), signature.end(), componentId);
    if (adding) {
      signature.insert(position, componentId);
    } else {
      signature.erase(position);
    }

    detail::Archetype* destination = findOrCreate(std::move(signature));
    return edges.insert(componentId, detail::buildEdge(source, *destination, adding ? componentId : detail::NoComponent));
  }

  bool World::checkInvariants(std::string* why) const {
    if (iterating_ != 0) {
      return fail(why, "I-11", "iterating=" + std::to_string(iterating_));
    }

    return checkArchetypes(why) && checkRecords(why) && checkFreeList(why) && checkComponentIndex(why) && checkQueries(why)
      && checkEdges(why);
  }

  bool World::checkArchetypes(std::string* why) const {
    for (size_t i = 0; i < archetypes_.size(); ++i) {
      const detail::Archetype& archetype = *archetypes_[i];

      auto it = bySignature_.find(archetype.signature);
      if (archetype.id != i || it == bySignature_.end() || it->second != &archetype) {
        return fail(why, "I-6", archetypeLabel(archetype));
      }

      if (!checkTableShape(archetype, why) || !checkColumnTypes(archetype, why) || !checkRows(archetype, why)) {
        return false;
      }
    }

    if (bySignature_.size() != archetypes_.size()) {
      return fail(why, "I-6", "tamanhos diferentes");
    }

    return true;
  }

  bool World::checkRows(const detail::Archetype& archetype, std::string* why) const {
    const detail::Table& table = archetype.table;
    for (uint32_t row = 0; row < table.count(); ++row) {
      Entity entity = table.entities()[row];
      if (entity.index >= store_.slotCount()) {
        return fail(why, "I-4", archetypeLabel(archetype) + " row=" + std::to_string(row));
      }

      const detail::EntityRecord& record = store_.record(entity.index);
      if (record.archetype != &archetype || record.row != row || record.generation != entity.generation) {
        return fail(why, "I-4", entityLabel(entity.index, entity.generation) + " " + archetypeLabel(archetype) + " row=" + std::to_string(row));
      }
    }

    return true;
  }

  bool World::checkRecords(std::string* why) const {
    for (uint32_t i = 0; i < store_.slotCount(); ++i) {
      const detail::EntityRecord& record = store_.record(i);
      if (!record.archetype) {
        continue;
      }

      const detail::Table& table = record.archetype->table;
      if (record.row >= table.count() || table.entities()[record.row] != Entity{i, record.generation}) {
        return fail(why, "I-3", entityLabel(i, record.generation));
      }
    }

    return true;
  }

  bool World::checkFreeList(std::string* why) const {
    std::vector<uint32_t> freeList(store_.freeList().begin(), store_.freeList().end());
    for (uint32_t slot : freeList) {
      if (slot >= store_.slotCount() || store_.record(slot).archetype != nullptr) {
        return fail(why, "I-10", "slot=" + std::to_string(slot));
      }
    }

    std::sort(freeList.begin(), freeList.end());
    if (std::adjacent_find(freeList.begin(), freeList.end()) != freeList.end()) {
      return fail(why, "I-10", "repetido");
    }

    return true;
  }

  bool World::checkComponentIndex(std::string* why) const {
    for (const auto& owned : archetypes_) {
      for (ComponentId componentId : owned->signature) {
        const uint16_t* column = componentId < componentIndex_.size() ? componentIndex_[componentId].find(owned->id) : nullptr;
        if (!column || *column != detail::columnOf(*owned, componentId)) {
          return fail(why, "I-7", "cid=" + std::to_string(componentId) + " " + archetypeLabel(*owned));
        }
      }
    }

    for (size_t componentId = 0; componentId < componentIndex_.size(); ++componentId) {
      for (const auto& [archetypeId, column] : componentIndex_[componentId]) {
        if (archetypeId >= archetypes_.size()) {
          return fail(why, "I-7", "cid=" + std::to_string(componentId));
        }

        const detail::Archetype& archetype = *archetypes_[archetypeId];
        if (!std::binary_search(archetype.signature.begin(), archetype.signature.end(), ComponentId(componentId)) || column != detail::columnOf(archetype, ComponentId(componentId))) {
          return fail(why, "I-7", "cid=" + std::to_string(componentId) + " " + archetypeLabel(archetype));
        }
      }
    }

    return true;
  }

  bool World::checkQueries(std::string* why) const {
    for (size_t queryIndex = 0; queryIndex < queries_.size(); ++queryIndex) {
      for (const auto& owned : archetypes_) {
        if (queries_[queryIndex]->isMatched(owned.get()) != queries_[queryIndex]->matches(*owned)) {
          return fail(why, "I-8", "query=" + std::to_string(queryIndex) + " " + archetypeLabel(*owned));
        }
      }
    }

    return true;
  }

  bool World::checkEdges(std::string* why) const {
    for (const auto& owned : archetypes_) {
      if (!checkCachedEdges(*owned, why)) {
        return false;
      }
    }

    for (const auto& [key, edge] : jumpEdges_) {
      const uint32_t sourceId = static_cast<uint32_t>(key >> 32), destinationId = static_cast<uint32_t>(key);
      const std::string where = "jump " + std::to_string(sourceId) + "->" + std::to_string(destinationId);
      if (sourceId >= archetypes_.size() || destinationId >= archetypes_.size()) {
        return fail(why, "I-9", where + " id");
      }
      if (edge.target != archetypes_[destinationId].get()) {
        return fail(why, "I-9", where + " destino");
      }
      if (!checkEdgeMapping(*archetypes_[sourceId], *archetypes_[destinationId], edge)) {
        return fail(why, "I-9", where + " sourceToDestination");
      }
      if (edge.addedColumn != -1) {
        return fail(why, "I-9", where + " addedColumn");
      }
    }

    return true;
  }

  namespace {
    void validateRawQueryComponents(const std::vector<ComponentId>& read, const std::vector<ComponentId>& write) {
      std::vector<ComponentId> seen;
      for (const std::vector<ComponentId>* group : {&read, &write}) {
        for (ComponentId componentId : *group) {
          OLY_ASSERT(componentId < registeredTypeCount(), "rawQuery componente desconhecido cid=%u", componentId);
          [[maybe_unused]] const TypeInfo& info = typeInfo(componentId);
          OLY_ASSERT(!info.tag, "rawQuery tag em read/write não é permitido type=%.*s", int(info.name.size()), info.name.data());
          OLY_ASSERT(std::find(seen.begin(), seen.end(), componentId) == seen.end(), "rawQuery componente repetido type=%.*s",
                     int(info.name.size()), info.name.data());
          seen.push_back(componentId);
        }
      }
    }
  }

  RawQuery& World::rawQuery(const RawQuerySpec& spec) {
    validateRawQueryComponents(spec.read, spec.write);

    std::vector<ComponentId> components = spec.read;
    components.insert(components.end(), spec.write.begin(), spec.write.end());

    detail::Signature with = components, without = spec.without;
    with.insert(with.end(), spec.with.begin(), spec.with.end());
    detail::sortUnique(with);
    detail::sortUnique(without);

    QueryKey key{{}, with, without};
    for (ComponentId componentId : spec.read) {
      key.components.push_back(uint64_t(componentId) << 1 | 1);
    }
    for (ComponentId componentId : spec.write) {
      key.components.push_back(uint64_t(componentId) << 1);
    }
    if (auto it = rawQueryCache_.find(key); it != rawQueryCache_.end()) {
      return *it->second;
    }

    auto created = std::make_unique<RawQuery>(&iterating_, std::move(components), with, std::move(without));
    created->initialize(archetypes_, smallestIndex(with));
    RawQuery& reference = *created;
    queries_.push_back(&reference);
    rawQueryCache_.emplace(std::move(key), std::move(created));
    return reference;
  }

  detail::QueryCacheBase* World::registerQuery(QueryKey key, std::unique_ptr<detail::QueryCacheBase> query) {
    detail::QueryCacheBase* registered = query.get();

    queries_.push_back(registered);
    queryCache_.emplace(std::move(key), std::move(query));

    return registered;
  }

  // Índice do componente de `with` presente em menos arquétipos (R-QY-3); nullptr se `with` é vazio.
  const core::SmallMap<detail::ArchetypeId, uint16_t>* World::smallestIndex(const detail::Signature& with) const {
    static const core::SmallMap<detail::ArchetypeId, uint16_t> empty;

    const core::SmallMap<detail::ArchetypeId, uint16_t>* best = nullptr;
    for (ComponentId componentId : with) {
      const auto* index = componentId < componentIndex_.size() ? &componentIndex_[componentId] : &empty;
      if (!best || index->size() < best->size()) {
        best = index;
      }
    }

    return best;
  }

  void World::materialize(Entity reserved) {
    detail::EntityRecord& record = store_.record(reserved.index);
    detail::Archetype& rootArchetype = root();

    record.row = rootArchetype.table.pushUninitialized(reserved);
    record.archetype = &rootArchetype;
  }

  void World::destroyOrRelease(Entity entity, bool alive) {
    if (alive) {
      destroy(entity);
    } else {
      store_.release(entity.index);
    }
  }

  uint32_t World::transitionTo(Entity entity, detail::EntityRecord& record, detail::Archetype& destination, const detail::Edge* edge) {
    if (!record.archetype) {
      record.row = destination.table.pushUninitialized(entity);
      record.archetype = &destination;
    } else if (edge) {
      moveEntity(entity, record, destination, *edge);
    }

    return record.row;
  }

  void World::flushSequential(CommandBuffer& commandBuffer) {
    OLY_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    OLY_ASSERT(commandBuffer.store() == &store_, "flush de buffer criado por outro World");

    commandBuffer.forEach([&](const detail::CommandView& command) {
      switch (command.operation) {
      case detail::CommandOperation::Spawn:
        if (store_.isReserved(command.entity)) {
          materialize(command.entity);
        }
        break;
      case detail::CommandOperation::Destroy:
        if (store_.isAlive(command.entity)) {
          destroy(command.entity);
        } else if (store_.isReserved(command.entity)) {
          store_.release(command.entity.index);
        }
        break;
      case detail::CommandOperation::Add:
        if (store_.isAlive(command.entity)) {
          addRaw(command.entity, command.componentId, command.payload);
        }
        break;
      case detail::CommandOperation::Remove:
        if (store_.isAlive(command.entity)) {
          removeRaw(command.entity, command.componentId);
        }
        break;
      }
    });

    commandBuffer.clear();
  }

  const detail::Edge& World::jumpEdge(detail::Archetype& source, detail::Archetype& destination) {
    const uint64_t key = (uint64_t(source.id) << 32) | destination.id;
    if (auto it = jumpEdges_.find(key); it != jumpEdges_.end()) {
      return it->second;
    }

    return jumpEdges_.emplace(key, detail::buildEdge(source, destination, detail::NoComponent)).first->second;
  }

  void World::flush(CommandBuffer& commandBuffer) {
    OLY_ASSERT(iterating_ == 0, "flush durante iteração=%u", iterating_);
    OLY_ASSERT(commandBuffer.store() == &store_, "flush de buffer criado por outro World");

    const detail::CommandPlan plan = commandBuffer.plan();
    detail::NetEffect net;
    FlushMemo memo;

    for (const detail::EntityPlan& entityPlan : plan.entities) {
      const Entity entity = entityPlan.entity;
      commandBuffer.netEffect(plan, entityPlan, net);

      const bool alive = store_.isAlive(entity);
      const bool reserved = net.spawned && store_.isReserved(entity);
      if (!alive && !reserved) {
        continue; // handle obsoleto: ignorado em vez de cancelar os comandos no destroy (spec §10, questão 3)
      }

      if (net.destroyed) {
        destroyOrRelease(entity, alive);
        continue;
      }

      const std::span<const detail::NetComponentOperation> operations = net.operations;
      detail::EntityRecord& record = store_.record(entity.index);
      detail::Archetype* source = record.archetype; // nullptr se reservada
      if (!memo.matches(source, operations)) {
        detail::Archetype* destination = findOrCreate(applyOperations(source ? source->signature : detail::Signature{}, operations));
        memo.assign(source, operations, destination, (source && destination != source) ? &jumpEdge(*source, *destination) : nullptr);
      }

      const uint32_t row = transitionTo(entity, record, *memo.destination, memo.edge);
      writeValues(memo, operations, row);
    }

    commandBuffer.clear();
  }
}
