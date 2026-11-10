#include "oly/ecs/command_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "oly/core/align.hpp"
#include "oly/core/assert.hpp"
#include "oly/ecs/detail/table.hpp"

namespace oly::ecs {
  namespace {
    constexpr size_t kHeaderSize = 24;
    constexpr size_t kBlockAlignment = 64;
  }

  CommandBuffer::~CommandBuffer() {
    if (!store_) {
      return;
    }

    releasePendingSpawns();
    clear();
    freeBlock();
  }

  void CommandBuffer::releasePendingSpawns() noexcept {
    for (Entity entity : spawned_) {
      if (store_->isReserved(entity)) {
        store_->release(entity.index);
      }
    }
  }

  CommandBuffer::CommandBuffer(CommandBuffer&& other) noexcept
    : store_(other.store_),
      data_(other.data_),
      size_(other.size_),
      capacity_(other.capacity_),
      count_(other.count_),
      hasNonTrivialPayload_(other.hasNonTrivialPayload_),
      spawned_(std::move(other.spawned_)),
      grouped_(other.grouped_),
      runs_(std::move(other.runs_)),
      seen_(std::move(other.seen_)) {
    other.store_ = nullptr;
    other.data_ = nullptr;
    other.size_ = other.capacity_ = other.count_ = 0;
    other.hasNonTrivialPayload_ = false;
    other.spawned_.clear();
    other.runs_.clear();
    other.seen_.clear();
    other.grouped_ = true;
  }

  CommandBuffer& CommandBuffer::operator=(CommandBuffer&& other) noexcept {
    if (this != &other) {
      if (store_) {
        releasePendingSpawns();
        clear();
        freeBlock();
      }

      store_ = other.store_;
      data_ = other.data_;
      size_ = other.size_;
      capacity_ = other.capacity_;
      count_ = other.count_;
      hasNonTrivialPayload_ = other.hasNonTrivialPayload_;
      spawned_ = std::move(other.spawned_);
      grouped_ = other.grouped_;
      runs_ = std::move(other.runs_);
      seen_ = std::move(other.seen_);

      other.store_ = nullptr;
      other.data_ = nullptr;
      other.size_ = other.capacity_ = other.count_ = 0;
      other.hasNonTrivialPayload_ = false;
      other.spawned_.clear();
      other.runs_.clear();
      other.seen_.clear();
      other.grouped_ = true;
    }

    return *this;
  }

  void CommandBuffer::freeBlock() noexcept {
    if (data_) {
      core::alignedFree(data_, kBlockAlignment);
    }

    data_ = nullptr;
    size_ = capacity_ = 0;
  }

  size_t CommandBuffer::readAt(size_t offset, detail::CommandView& out) const noexcept {
    const std::byte* header = data_ + offset;
    uint64_t entityBits;
    uint32_t size;
    out.operation = static_cast<detail::CommandOperation>(header[0]);
    std::memcpy(&entityBits, header + 8, 8);
    std::memcpy(&out.componentId, header + 16, 4);
    std::memcpy(&size, header + 20, 4);
    out.entity = Entity{static_cast<uint32_t>(entityBits), static_cast<uint32_t>(entityBits >> 32)};

    if (out.operation == detail::CommandOperation::Add && size > 0) {
      const size_t payloadOffset = core::alignUp(offset + kHeaderSize, std::max<size_t>(8, static_cast<uint8_t>(header[1])));
      out.payload = const_cast<std::byte*>(data_) + payloadOffset;
      return core::alignUp(payloadOffset + size, 8);
    }

    out.payload = nullptr;
    return offset + kHeaderSize;
  }

  void CommandBuffer::grow(size_t need) {
    size_t newCapacity = std::max<size_t>(1024, capacity_ * 2);
    while (newCapacity < need) {
      newCapacity *= 2;
    }

    std::byte* fresh = core::alignedAllocate(newCapacity, kBlockAlignment);
    if (!hasNonTrivialPayload_) {
      if (size_ > 0) {
        std::memcpy(fresh, data_, size_);
      }
    } else {
      for (size_t offset = 0; offset < size_;) {
        detail::CommandView command;
        const size_t next = readAt(offset, command);
        std::memcpy(fresh + offset, data_ + offset, kHeaderSize);
        if (command.payload) {
          const TypeInfo& info = typeInfo(command.componentId);
          detail::relocateValue(info, fresh + (static_cast<std::byte*>(command.payload) - data_), command.payload);
        }
        offset = next;
      }
    }

    if (data_) {
      core::alignedFree(data_, kBlockAlignment);
    }

    data_ = fresh;
    capacity_ = newCapacity;
  }

  std::byte* CommandBuffer::pushRecord(detail::CommandOperation operation, Entity entity, ComponentId componentId, uint32_t size, uint32_t alignment, bool trivial) {
    const size_t offset = size_;
    size_t payloadOffset = offset + kHeaderSize, next = offset + kHeaderSize;
    if (size > 0) {
      payloadOffset = core::alignUp(offset + kHeaderSize, std::max<size_t>(8, alignment));
      next = core::alignUp(payloadOffset + size, 8);
    }

    if (next > capacity_) {
      grow(next);
    }

    if (grouped_) {
      if (!runs_.empty() && runs_.back().entity == entity) {
        ++runs_.back().count;
      } else {
        const size_t word = entity.index >> 6;
        if (word >= seen_.size()) {
          seen_.resize(std::max(word + 1, seen_.size() * 2), 0);
        }

        const uint64_t bit = uint64_t(1) << (entity.index & 63);
        if (seen_[word] & bit) {
          grouped_ = false; // entidade voltou fora do trecho atual: plan() agrupa por counting sort
          runs_.clear();
        } else {
          seen_[word] |= bit;
          runs_.push_back(detail::EntityPlan{entity, static_cast<uint32_t>(offset >> 3), 1});
        }
      }
    }

    std::byte* header = data_ + offset;
    const uint64_t entityBits = (uint64_t(entity.generation) << 32) | entity.index;
    header[0] = static_cast<std::byte>(operation);
    std::memset(header + 1, 0, 7);
    header[1] = static_cast<std::byte>(alignment); // para readAt não consultar o TypeInfo
    std::memcpy(header + 8, &entityBits, 8);
    std::memcpy(header + 16, &componentId, 4);
    std::memcpy(header + 20, &size, 4);

    size_ = next;
    ++count_;
    if (!trivial) {
      hasNonTrivialPayload_ = true;
    }

    return size > 0 ? data_ + payloadOffset : nullptr;
  }

  Entity CommandBuffer::spawn() {
    Entity entity = store_->allocate();
    pushRecord(detail::CommandOperation::Spawn, entity, 0, 0, 1, true);
    spawned_.push_back(entity);
    return entity;
  }

  void CommandBuffer::destroy(Entity entity) {
    pushRecord(detail::CommandOperation::Destroy, entity, 0, 0, 1, true);
  }

  void CommandBuffer::addById(Entity entity, ComponentId componentId, void* value) {
    const TypeInfo& info = typeInfo(componentId);
    void* payload = pushRecord(detail::CommandOperation::Add, entity, componentId, info.size, info.tag ? 1 : info.alignment, info.trivial);
    if (info.tag) {
      return;
    }

    if (info.trivial) {
      std::memcpy(payload, value, info.size);
    } else {
      info.moveConstruct(payload, value);
    }
  }

  void CommandBuffer::removeById(Entity entity, ComponentId componentId) {
    pushRecord(detail::CommandOperation::Remove, entity, componentId, 0, 1, true);
  }

  void CommandBuffer::clear() noexcept {
    if (hasNonTrivialPayload_) {
      for (size_t offset = 0; offset < size_;) {
        detail::CommandView command;
        offset = readAt(offset, command);
        if (command.payload) {
          detail::destroyValue(typeInfo(command.componentId), command.payload);
        }
      }
    }

    hasNonTrivialPayload_ = false;
    grouped_ = true;
    runs_.clear();
    std::fill(seen_.begin(), seen_.end(), 0);
    size_ = 0;
    count_ = 0;
    spawned_.clear();
  }

  detail::CommandPlan CommandBuffer::plan() const {
    constexpr uint32_t None = UINT32_MAX;
    detail::CommandPlan result;
    if (grouped_) {
      result.entities = runs_;
      return result;
    }

    std::vector<uint32_t> planBySlot(store_->slotCount(), None);
    std::unordered_map<uint64_t, uint32_t> overflow; // geração diferente no mesmo índice / índice fora da faixa
    auto planOf = [&](Entity entity) -> uint32_t {
      auto make = [&] {
        result.owned.push_back(detail::EntityPlan{entity});
        return static_cast<uint32_t>(result.owned.size() - 1);
      };

      if (entity.index < planBySlot.size()) {
        uint32_t& planIndex = planBySlot[entity.index];
        if (planIndex == None) {
          planIndex = make();
        }
        if (result.owned[planIndex].entity == entity) {
          return planIndex;
        }
      }

      const uint64_t key = (uint64_t(entity.generation) << 32) | entity.index;
      auto [it, inserted] = overflow.try_emplace(key, None);
      if (inserted) {
        it->second = make();
      }
      return it->second;
    };

    for (size_t offset = 0; offset < size_;) {
      detail::CommandView command;
      const size_t next = readAt(offset, command);
      ++result.owned[planOf(command.entity)].count;
      offset = next;
    }

    uint32_t accumulated = 0;
    for (detail::EntityPlan& entityPlan : result.owned) {
      entityPlan.begin = accumulated;
      accumulated += entityPlan.count;
    }

    // `begin` serve de cursor de escrita e é restaurado logo abaixo.
    result.order.resize(accumulated);
    for (size_t offset = 0; offset < size_;) {
      detail::CommandView command;
      const size_t next = readAt(offset, command);
      result.order[result.owned[planOf(command.entity)].begin++] = static_cast<uint32_t>(offset >> 3);
      offset = next;
    }
    for (detail::EntityPlan& entityPlan : result.owned) {
      entityPlan.begin -= entityPlan.count;
    }

    result.entities = result.owned;
    result.contiguous = false;
    return result;
  }

  void CommandBuffer::netEffect(const detail::CommandPlan& plan, const detail::EntityPlan& entityPlan, detail::NetEffect& out) const {
    out.spawned = out.destroyed = false;
    out.operations.clear();

    size_t offset = static_cast<size_t>(entityPlan.begin) << 3;
    for (uint32_t i = 0; i < entityPlan.count; ++i) {
      detail::CommandView command;
      if (plan.contiguous) {
        offset = readAt(offset, command);
      } else {
        readAt(static_cast<size_t>(plan.order[entityPlan.begin + i]) << 3, command);
      }

      if (command.operation == detail::CommandOperation::Spawn) {
        out.spawned = true;
        continue;
      }
      if (out.destroyed) {
        OLY_ASSERT(false, "command after destroy e=%u:%u", command.entity.index, command.entity.generation);
        continue; // sem asserts: ignorado (R-CB-4)
      }
      if (command.operation == detail::CommandOperation::Destroy) {
        out.operations.clear();
        out.destroyed = true;
        continue;
      }

      const detail::NetComponentOperation net{command.componentId, command.operation == detail::CommandOperation::Add, command.operation == detail::CommandOperation::Add ? command.payload : nullptr};
      if (out.operations.empty() || out.operations.back().componentId < command.componentId) { // caso comum: ids crescentes
        out.operations.push_back(net);
        continue;
      }

      auto it = std::lower_bound(out.operations.begin(), out.operations.end(), command.componentId,
                                 [](const detail::NetComponentOperation& operation, ComponentId componentId) { return operation.componentId < componentId; });
      if (it != out.operations.end() && it->componentId == command.componentId) {
        *it = net;
      } else {
        out.operations.insert(it, net);
      }
    }
  }
}
