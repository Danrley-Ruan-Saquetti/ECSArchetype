#include <ecs/command_buffer.hpp>

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include <ecs/detail/align.hpp>
#include <ecs/detail/assert.hpp>
#include <ecs/table.hpp>

namespace ecs {
namespace {
constexpr size_t kHeader = 24;
constexpr size_t kBlockAlign = 64;
}  // namespace

CommandBuffer::~CommandBuffer() {
    if (!store_) return;  // moved-from
    for (Entity e : spawned_)
        if (store_->isReserved(e)) store_->release(e.index);  // R-CB-2: índices reservados voltam à freeList
    clear();
    freeBlock();
}

CommandBuffer::CommandBuffer(CommandBuffer&& o) noexcept
    : store_(o.store_), data_(o.data_), size_(o.size_), cap_(o.cap_), count_(o.count_), spawned_(std::move(o.spawned_)) {
    o.store_ = nullptr;
    o.data_ = nullptr;
    o.size_ = o.cap_ = o.count_ = 0;
    o.spawned_.clear();
}

CommandBuffer& CommandBuffer::operator=(CommandBuffer&& o) noexcept {
    if (this != &o) {
        if (store_) {
            for (Entity e : spawned_)
                if (store_->isReserved(e)) store_->release(e.index);
            clear();
            freeBlock();
        }
        store_ = o.store_;
        data_ = o.data_;
        size_ = o.size_;
        cap_ = o.cap_;
        count_ = o.count_;
        spawned_ = std::move(o.spawned_);
        o.store_ = nullptr;
        o.data_ = nullptr;
        o.size_ = o.cap_ = o.count_ = 0;
        o.spawned_.clear();
    }
    return *this;
}

void CommandBuffer::freeBlock() noexcept {
    if (data_) alignedFree(data_, kBlockAlign);
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
        const size_t payloadOff = alignUp(off + kHeader, std::max<size_t>(8, typeInfo(out.cid).align));
        out.payload = const_cast<std::byte*>(data_) + payloadOff;
        return alignUp(payloadOff + size, 8);
    }
    out.payload = nullptr;
    return off + kHeader;
}

void CommandBuffer::grow(size_t need) {
    size_t newCap = std::max<size_t>(1024, cap_ * 2);
    while (newCap < need) newCap *= 2;
    std::byte* fresh = alignedAlloc(newCap, kBlockAlign);
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
    if (data_) alignedFree(data_, kBlockAlign);
    data_ = fresh;
    cap_ = newCap;
}

std::byte* CommandBuffer::pushRecord(CommandOp op, Entity e, ComponentId cid, uint32_t size, uint32_t align) {
    const size_t off = size_;
    size_t payloadOff = off + kHeader, next = off + kHeader;
    if (size > 0) {
        payloadOff = alignUp(off + kHeader, std::max<size_t>(8, align));
        next = alignUp(payloadOff + size, 8);
    }
    if (next > cap_) grow(next);
    std::byte* h = data_ + off;
    const uint64_t eb = (uint64_t(e.generation) << 32) | e.index;
    h[0] = static_cast<std::byte>(op);
    std::memset(h + 1, 0, 7);
    std::memcpy(h + 8, &eb, 8);
    std::memcpy(h + 16, &cid, 4);
    std::memcpy(h + 20, &size, 4);
    size_ = next;
    ++count_;
    return size > 0 ? data_ + payloadOff : nullptr;
}

Entity CommandBuffer::spawn() {
    Entity e = store_->allocate();
    pushRecord(CommandOp::Spawn, e, 0, 0, 1);
    spawned_.push_back(e);
    return e;
}

void CommandBuffer::destroy(Entity e) { pushRecord(CommandOp::Destroy, e, 0, 0, 1); }

void CommandBuffer::removeRaw(Entity e, ComponentId cid) { pushRecord(CommandOp::Remove, e, cid, 0, 1); }

void CommandBuffer::clear() noexcept {
    for (size_t off = 0; off < size_;) {
        CommandView v;
        off = readAt(off, v);
        if (v.payload) destroyValue(typeInfo(v.cid), v.payload);
    }
    size_ = 0;
    count_ = 0;
    spawned_.clear();
}

}  // namespace ecs

namespace ecs {

CommandPlan CommandBuffer::plan() const {
    constexpr uint32_t None = UINT32_MAX;
    struct Cmd {
        CommandView v;
        uint32_t next;
    };
    CommandPlan p;
    std::vector<Cmd> cmds;
    cmds.reserve(count_);
    std::vector<uint32_t> head, tail;  // por plano: primeiro e último comando
    std::vector<uint32_t> slotOf(store_->slotCount(), None);  // índice da entidade → plano
    std::unordered_map<uint64_t, uint32_t> overflow;          // geração diferente no mesmo índice / índice fora da faixa

    auto newPlan = [&](Entity e) {
        p.entities.push_back(EntityPlan{e});
        head.push_back(None);
        tail.push_back(None);
        return static_cast<uint32_t>(p.entities.size() - 1);
    };
    auto planOf = [&](Entity e) -> uint32_t {
        if (e.index < slotOf.size()) {
            uint32_t& s = slotOf[e.index];
            if (s == None) s = newPlan(e);
            if (p.entities[s].e == e) return s;
        }
        const uint64_t key = (uint64_t(e.generation) << 32) | e.index;
        auto [it, inserted] = overflow.try_emplace(key, None);
        if (inserted) it->second = newPlan(e);
        return it->second;
    };

    // Passada 1a: encadeia os comandos de cada entidade, na ordem de gravação (estável, O(n)).
    for (size_t off = 0; off < size_;) {
        CommandView v;
        const size_t next = readAt(off, v);
        const auto ci = static_cast<uint32_t>(cmds.size());
        cmds.push_back(Cmd{v, None});
        const uint32_t pi = planOf(v.e);
        if (tail[pi] == None)
            head[pi] = ci;
        else
            cmds[tail[pi]].next = ci;
        tail[pi] = ci;
        off = next;
    }

    // Passada 1b: efeito líquido por entidade (R-CB-4), aplicado na ordem de gravação.
    for (uint32_t pi = 0; pi < p.entities.size(); ++pi) {
        EntityPlan& ep = p.entities[pi];
        ep.opBegin = static_cast<uint32_t>(p.ops.size());
        for (uint32_t ci = head[pi]; ci != None; ci = cmds[ci].next) {
            const CommandView& v = cmds[ci].v;
            if (v.op == CommandOp::Spawn) {
                ep.spawned = true;
                continue;
            }
            if (ep.destroyed) {
                ECS_ASSERT(false, "command after destroy e=%u:%u", v.e.index, v.e.generation);
                continue;  // sem asserts: ignorado (R-CB-4)
            }
            if (v.op == CommandOp::Destroy) {
                p.ops.resize(ep.opBegin);  // destroy descarta os comandos anteriores
                ep.destroyed = true;
                continue;
            }
            auto first = p.ops.begin() + ep.opBegin;
            auto it = std::lower_bound(first, p.ops.end(), v.cid,
                                       [](const NetComponentOp& op, ComponentId c) { return op.cid < c; });
            const NetComponentOp net{v.cid, v.op == CommandOp::Add, v.op == CommandOp::Add ? v.payload : nullptr};
            if (it != p.ops.end() && it->cid == v.cid)
                *it = net;  // o último comando por tipo vale
            else
                p.ops.insert(it, net);
        }
        ep.opCount = static_cast<uint32_t>(p.ops.size()) - ep.opBegin;
    }
    return p;
}

}  // namespace ecs
