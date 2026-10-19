#include "ecs/command_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "ecs/detail/align.hpp"
#include "ecs/detail/assert.hpp"
#include "ecs/table.hpp"

namespace ecs {
  namespace {
    constexpr size_t kHeader = 24;
    constexpr size_t kBlockAlign = 64;
  }

  CommandBuffer::~CommandBuffer() {
    if (!store_)
      return;

    releasePendingSpawns();
    clear();
    freeBlock();
  }

  void CommandBuffer::releasePendingSpawns() noexcept {
    for (Entity e : spawned_)
      if (store_->isReserved(e))
        store_->release(e.index);
  }

  CommandBuffer::CommandBuffer(CommandBuffer&& o) noexcept
    : store_(o.store_),
      data_(o.data_),
      size_(o.size_),
      cap_(o.cap_),
      count_(o.count_),
      hasNonTrivialPayload_(o.hasNonTrivialPayload_),
      spawned_(std::move(o.spawned_)),
      grouped_(o.grouped_),
      runs_(std::move(o.runs_)),
      seen_(std::move(o.seen_)) {
    o.store_ = nullptr;
    o.data_ = nullptr;
    o.size_ = o.cap_ = o.count_ = 0;
    o.hasNonTrivialPayload_ = false;
    o.spawned_.clear();
    o.runs_.clear();
    o.seen_.clear();
    o.grouped_ = true;
  }

  CommandBuffer& CommandBuffer::operator=(CommandBuffer&& o) noexcept {
    if (this != &o) {
      if (store_) {
        releasePendingSpawns();
        clear();
        freeBlock();
      }

      store_ = o.store_;
      data_ = o.data_;
      size_ = o.size_;
      cap_ = o.cap_;
      count_ = o.count_;
      hasNonTrivialPayload_ = o.hasNonTrivialPayload_;
      spawned_ = std::move(o.spawned_);
      grouped_ = o.grouped_;
      runs_ = std::move(o.runs_);
      seen_ = std::move(o.seen_);

      o.store_ = nullptr;
      o.data_ = nullptr;
      o.size_ = o.cap_ = o.count_ = 0;
      o.hasNonTrivialPayload_ = false;
      o.spawned_.clear();
      o.runs_.clear();
      o.seen_.clear();
      o.grouped_ = true;
    }

    return *this;
  }

  void CommandBuffer::freeBlock() noexcept {
    if (data_)
      alignedFree(data_, kBlockAlign);

    data_ = nullptr;
    size_ = cap_ = 0;
  }

  size_t CommandBuffer::readAt(size_t off, CommandView& out) const noexcept {
    const std::byte* h = data_ + off;
    uint64_t eb;
    uint32_t size;
    out.op = static_cast<CommandOp>(h[0]);
    std::memcpy(&eb, h + 8, 8);
    std::memcpy(&out.cid, h + 16, 4);
    std::memcpy(&size, h + 20, 4);
    out.e = Entity{static_cast<uint32_t>(eb), static_cast<uint32_t>(eb >> 32)};

    if (out.op == CommandOp::Add && size > 0) {
      const size_t payloadOff = alignUp(off + kHeader, std::max<size_t>(8, static_cast<uint8_t>(h[1])));
      out.payload = const_cast<std::byte*>(data_) + payloadOff;
      return alignUp(payloadOff + size, 8);
    }

    out.payload = nullptr;
    return off + kHeader;
  }

  void CommandBuffer::grow(size_t need) {
    size_t newCap = std::max<size_t>(1024, cap_ * 2);
    while (newCap < need)
      newCap *= 2;

    std::byte* fresh = alignedAlloc(newCap, kBlockAlign);
    if (!hasNonTrivialPayload_) {
      if (size_ > 0)
        std::memcpy(fresh, data_, size_);
    } else {
      for (size_t off = 0; off < size_;) {
        CommandView v;
        const size_t next = readAt(off, v);
        std::memcpy(fresh + off, data_ + off, kHeader);
        if (v.payload) {
          const TypeInfo& ti = typeInfo(v.cid);
          relocateValue(ti, fresh + (static_cast<std::byte*>(v.payload) - data_), v.payload);
        }
        off = next;
      }
    }

    if (data_)
      alignedFree(data_, kBlockAlign);
    data_ = fresh;
    cap_ = newCap;
  }

  std::byte* CommandBuffer::pushRecord(CommandOp op, Entity e, ComponentId cid, uint32_t size, uint32_t align, bool trivial) {
    const size_t off = size_;
    size_t payloadOff = off + kHeader, next = off + kHeader;
    if (size > 0) {
      payloadOff = alignUp(off + kHeader, std::max<size_t>(8, align));
      next = alignUp(payloadOff + size, 8);
    }

    if (next > cap_)
      grow(next);

    if (grouped_) {
      if (!runs_.empty() && runs_.back().e == e) {
        ++runs_.back().count;
      } else {
        const size_t word = e.index >> 6;
        if (word >= seen_.size())
          seen_.resize(std::max(word + 1, seen_.size() * 2), 0);

        const uint64_t bit = uint64_t(1) << (e.index & 63);
        if (seen_[word] & bit) {
          grouped_ = false; // entidade voltou fora do trecho atual: plan() agrupa por counting sort
          runs_.clear();
        } else {
          seen_[word] |= bit;
          runs_.push_back(EntityPlan{e, static_cast<uint32_t>(off >> 3), 1});
        }
      }
    }

    std::byte* h = data_ + off;
    const uint64_t eb = (uint64_t(e.generation) << 32) | e.index;
    h[0] = static_cast<std::byte>(op);
    std::memset(h + 1, 0, 7);
    h[1] = static_cast<std::byte>(align); // para readAt não consultar o TypeInfo
    std::memcpy(h + 8, &eb, 8);
    std::memcpy(h + 16, &cid, 4);
    std::memcpy(h + 20, &size, 4);

    size_ = next;
    ++count_;
    if (!trivial)
      hasNonTrivialPayload_ = true;

    return size > 0 ? data_ + payloadOff : nullptr;
  }

  Entity CommandBuffer::spawn() {
    Entity e = store_->allocate();
    pushRecord(CommandOp::Spawn, e, 0, 0, 1, true);
    spawned_.push_back(e);
    return e;
  }

  void CommandBuffer::destroy(Entity e) {
    pushRecord(CommandOp::Destroy, e, 0, 0, 1, true);
  }

  void CommandBuffer::removeRaw(Entity e, ComponentId cid) {
    pushRecord(CommandOp::Remove, e, cid, 0, 1, true);
  }

  void CommandBuffer::clear() noexcept {
    if (hasNonTrivialPayload_) {
      for (size_t off = 0; off < size_;) {
        CommandView v;
        off = readAt(off, v);
        if (v.payload)
          destroyValue(typeInfo(v.cid), v.payload);
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

  CommandPlan CommandBuffer::plan() const {
    constexpr uint32_t None = UINT32_MAX;
    CommandPlan p;
    if (grouped_) {
      p.entities = runs_;
      return p;
    }

    std::vector<uint32_t> planBySlot(store_->slotCount(), None);
    std::unordered_map<uint64_t, uint32_t> overflow; // geração diferente no mesmo índice / índice fora da faixa
    auto planOf = [&](Entity e) -> uint32_t {
      auto make = [&] {
        p.owned.push_back(EntityPlan{e});
        return static_cast<uint32_t>(p.owned.size() - 1);
      };

      if (e.index < planBySlot.size()) {
        uint32_t& s = planBySlot[e.index];
        if (s == None)
          s = make();
        if (p.owned[s].e == e)
          return s;
      }

      const uint64_t key = (uint64_t(e.generation) << 32) | e.index;
      auto [it, inserted] = overflow.try_emplace(key, None);
      if (inserted)
        it->second = make();
      return it->second;
    };

    for (size_t off = 0; off < size_;) {
      CommandView v;
      const size_t next = readAt(off, v);
      ++p.owned[planOf(v.e)].count;
      off = next;
    }

    uint32_t acc = 0;
    for (EntityPlan& ep : p.owned) {
      ep.begin = acc;
      acc += ep.count;
    }

    // `begin` serve de cursor de escrita e é restaurado logo abaixo.
    p.order.resize(acc);
    for (size_t off = 0; off < size_;) {
      CommandView v;
      const size_t next = readAt(off, v);
      p.order[p.owned[planOf(v.e)].begin++] = static_cast<uint32_t>(off >> 3);
      off = next;
    }
    for (EntityPlan& ep : p.owned)
      ep.begin -= ep.count;

    p.entities = p.owned;
    p.contiguous = false;
    return p;
  }

  void CommandBuffer::netEffect(const CommandPlan& plan, const EntityPlan& ep, NetEffect& out) const {
    out.spawned = out.destroyed = false;
    out.ops.clear();

    size_t off = static_cast<size_t>(ep.begin) << 3;
    for (uint32_t i = 0; i < ep.count; ++i) {
      CommandView v;
      if (plan.contiguous)
        off = readAt(off, v);
      else
        readAt(static_cast<size_t>(plan.order[ep.begin + i]) << 3, v);

      if (v.op == CommandOp::Spawn) {
        out.spawned = true;
        continue;
      }
      if (out.destroyed) {
        ECS_ASSERT(false, "command after destroy e=%u:%u", v.e.index, v.e.generation);
        continue; // sem asserts: ignorado (R-CB-4)
      }
      if (v.op == CommandOp::Destroy) {
        out.ops.clear();
        out.destroyed = true;
        continue;
      }

      const NetComponentOp net{v.cid, v.op == CommandOp::Add, v.op == CommandOp::Add ? v.payload : nullptr};
      if (out.ops.empty() || out.ops.back().cid < v.cid) { // caso comum: ids crescentes
        out.ops.push_back(net);
        continue;
      }

      auto it = std::lower_bound(out.ops.begin(), out.ops.end(), v.cid,
                                 [](const NetComponentOp& op, ComponentId c) { return op.cid < c; });
      if (it != out.ops.end() && it->cid == v.cid)
        *it = net;
      else
        out.ops.insert(it, net);
    }
  }
}
