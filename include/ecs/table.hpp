#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <vector>

#include <ecs/entity.hpp>
#include <ecs/type_info.hpp>

namespace ecs {

// Move o valor de src para dst (dst não construído). Triviais: memcpy; demais: moveConstruct + destroy.
inline void relocateValue(const TypeInfo& ti, void* dst, void* src) noexcept {
    if (ti.trivial) {
        std::memcpy(dst, src, ti.size);
    } else {
        ti.moveConstruct(dst, src);
        ti.destroy(src);
    }
}
inline void destroyValue(const TypeInfo& ti, void* p) noexcept {
    if (!ti.trivial) ti.destroy(p);
}

struct Column {
    const TypeInfo* info;
    std::byte* data = nullptr;
    void* at(uint32_t r) noexcept { return data + size_t(r) * info->size; }
    const void* at(uint32_t r) const noexcept { return data + size_t(r) * info->size; }
};

class Table {
public:
    explicit Table(std::span<const ComponentId> columnTypes);
    ~Table();
    Table(Table&&) noexcept;
    Table& operator=(Table&&) noexcept;
    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    uint32_t count() const noexcept { return count_; }
    uint32_t capacity() const noexcept { return capacity_; }
    uint32_t pushUninit(Entity e);                          // linha nova MORTA no fim; pode realocar
    void destroyRow(uint32_t r) noexcept;                   // linha viva → morta
    std::optional<Entity> fillHole(uint32_t r) noexcept;    // tapa linha morta com a última

    std::span<const Entity> entities() const noexcept { return entities_; }
    Column& column(uint16_t c) noexcept { return columns_[c]; }
    const Column& column(uint16_t c) const noexcept { return columns_[c]; }
    uint16_t columnCount() const noexcept { return static_cast<uint16_t>(columns_.size()); }
    std::span<const ComponentId> columnTypes() const noexcept { return columnTypes_; }

private:
    void reserve(uint32_t newCap);  // realoca TODAS as colunas juntas
    void releaseAll() noexcept;     // destrói linhas vivas e libera memória

    std::vector<Column> columns_;
    std::vector<ComponentId> columnTypes_;
    std::vector<Entity> entities_;  // size() == count
    uint32_t count_ = 0, capacity_ = 0;
};

}  // namespace ecs
