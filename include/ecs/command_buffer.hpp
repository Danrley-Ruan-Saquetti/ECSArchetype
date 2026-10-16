#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>
#include <vector>

#include <ecs/entity.hpp>
#include <ecs/type_info.hpp>

namespace ecs {

enum class CommandOp : uint8_t { Spawn, Destroy, Add, Remove };

struct CommandView {
    CommandOp op;
    Entity e;
    ComponentId cid;
    void* payload;  // nullptr se não há payload (Spawn/Destroy/Remove/tag)
};

// Registro (alinhado a 8): [op:u8][pad:7][entity:u64][cid:u32][size:u32][payload alinhado].
// O bloco é alinhado a 64. Ao crescer, cada payload é realocado pelo seu TypeInfo
// (memcpy se trivial, moveConstruct + destroy caso contrário): um memmove cego quebraria
// tipos que apontam para si mesmos (ex.: std::string com SSO).
class CommandBuffer {
public:
    explicit CommandBuffer(EntityStore& store) noexcept : store_(&store) {}
    ~CommandBuffer();
    CommandBuffer(CommandBuffer&& o) noexcept;
    CommandBuffer& operator=(CommandBuffer&& o) noexcept;
    CommandBuffer(const CommandBuffer&) = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;

    // Reserva um índice imediatamente (R-CB-1); a entidade só fica viva no flush.
    Entity spawn();
    void destroy(Entity e);
    template <class T>
    void add(Entity e, T&& value);
    template <class T>
    void remove(Entity e) {
        removeRaw(e, componentId<T>());
    }
    void removeRaw(Entity e, ComponentId cid);

    // Visita os comandos em ordem de gravação.
    template <class F>
    void forEach(F&& f) {
        for (size_t off = 0; off < size_;) {
            CommandView v;
            off = readAt(off, v);
            f(static_cast<const CommandView&>(v));
        }
    }

    size_t commandCount() const noexcept { return count_; }
    bool empty() const noexcept { return count_ == 0; }
    const EntityStore* store() const noexcept { return store_; }

    // Destrói os payloads não triviais (inclusive moved-from), zera o buffer e esquece os spawns
    // (já materializados ou liberados pelo flush).
    void clear() noexcept;

private:
    std::byte* pushRecord(CommandOp op, Entity e, ComponentId cid, uint32_t size, uint32_t align);
    size_t readAt(size_t off, CommandView& out) const noexcept;  // devolve o offset do próximo registro
    void grow(size_t need);
    void freeBlock() noexcept;

    EntityStore* store_;
    std::byte* data_ = nullptr;
    size_t size_ = 0, cap_ = 0;
    size_t count_ = 0;
    std::vector<Entity> spawned_;
};

template <class T>
void CommandBuffer::add(Entity e, T&& value) {
    using U = std::remove_cvref_t<T>;
    const ComponentId cid = componentId<U>();
    if constexpr (std::is_empty_v<U>) {
        pushRecord(CommandOp::Add, e, cid, 0, 1);
    } else {
        void* p = pushRecord(CommandOp::Add, e, cid, static_cast<uint32_t>(sizeof(U)), static_cast<uint32_t>(alignof(U)));
        ::new (p) U(std::forward<T>(value));
    }
}

}  // namespace ecs
