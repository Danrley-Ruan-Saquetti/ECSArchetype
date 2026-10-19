#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "ecs/archetype.hpp"
#include "ecs/detail/small_map.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  // Filtros: tags (ou componentes) que o arquétipo deve / não deve ter, sem entregá-los ao sistema.
  template <class... Us>
  struct With {};
  template <class... Us>
  struct Without {};

  class QueryCacheBase {
   public:
    explicit QueryCacheBase(uint32_t* iterating)
      : iterating_(iterating) {}
    virtual ~QueryCacheBase() = default;

    virtual void onArchetypeCreated(Archetype& archetype) = 0;
    virtual bool matches(const Archetype& archetype) const = 0;
    virtual bool isMatched(const Archetype* archetype) const = 0;

   protected:
    uint32_t* iterating_;
  };

  namespace detail {
    struct IterationGuard {
      explicit IterationGuard(uint32_t* counter) noexcept
        : counter_(counter) {
        ++*counter_;
      }
      ~IterationGuard() {
        --*counter_;
      }
      IterationGuard(const IterationGuard&) = delete;
      IterationGuard& operator=(const IterationGuard&) = delete;

      uint32_t* counter_;
    };
  }

  template <class... Ts>
  struct MatchedTable {
    Archetype* archetype;
    std::array<uint16_t, sizeof...(Ts)> columns;
  };

  template <class... Ts>
  class Query : public QueryCacheBase {
    static_assert((!std::is_empty_v<std::remove_const_t<Ts>> && ...),
                  "tag em Query<Ts...> não é permitido; use With<Tag>/Without<Tag> (R-QY-2)");
    static_assert(((!std::is_reference_v<Ts> && !std::is_volatile_v<Ts>) && ...),
                  "Query<Ts...> aceita apenas T ou const T");

   public:
    // Sem uso na v1: reservado para o scheduler (R-QY-1).
    static constexpr std::array<bool, sizeof...(Ts)> writes{!std::is_const_v<Ts>...};

    // `with` = ids de Ts ∪ With<...>, ordenados e sem repetição; `without` idem.
    Query(uint32_t* iterating, std::vector<ComponentId> with, std::vector<ComponentId> without)
      : QueryCacheBase(iterating),
        with_(std::move(with)),
        without_(std::move(without)),
        componentIds_{componentId<std::remove_const_t<Ts>>()...} {}

    // Com `smallest`, percorre só os arquétipos do menor componentIndex (R-QY-3).
    void initialize(const std::vector<std::unique_ptr<Archetype>>& all, const SmallMap<ArchetypeId, uint16_t>* smallest) {
      if (smallest) {
        for (const auto& [archetypeId, column] : *smallest)
          onArchetypeCreated(*all[archetypeId]);
      } else {
        for (const auto& archetype : all)
          onArchetypeCreated(*archetype);
      }
    }

    size_t matchedCount() const noexcept {
      return matched_.size();
    }

    bool matches(const Archetype& archetype) const override {
      if (!std::includes(archetype.signature.begin(), archetype.signature.end(), with_.begin(), with_.end()))
        return false;
      for (ComponentId componentId : without_)
        if (std::binary_search(archetype.signature.begin(), archetype.signature.end(), componentId))
          return false;

      return true;
    }

    bool isMatched(const Archetype* archetype) const override {
      return std::any_of(matched_.begin(), matched_.end(), [archetype](const MatchedTable<Ts...>& matched) { return matched.archetype == archetype; });
    }

    void onArchetypeCreated(Archetype& archetype) override {
      if (!matches(archetype))
        return;

      MatchedTable<Ts...> matched{&archetype, {}};
      for (size_t i = 0; i < componentIds_.size(); ++i)
        matched.columns[i] = columnOf(archetype, componentIds_[i]);
      matched_.push_back(matched);
    }

    // callback(std::span<Ts>...) uma vez por tabela não vazia. Ts const → std::span<const T>.
    template <class F>
    void eachBlock(F&& callback) {
      detail::IterationGuard guard(iterating_);
      for (const MatchedTable<Ts...>& matched : matched_) {
        Table& table = matched.archetype->table;
        if (table.count() == 0)
          continue;

        callBlock(callback, table, matched, std::index_sequence_for<Ts...>{});
      }
    }

    // callback(Ts&...) ou callback(Entity, Ts&...).
    template <class F>
    void each(F&& callback) {
      detail::IterationGuard guard(iterating_);
      for (const MatchedTable<Ts...>& matched : matched_) {
        Table& table = matched.archetype->table;
        const uint32_t rowCount = table.count();
        if (rowCount == 0)
          continue;

        callRows(callback, table, matched, rowCount, std::index_sequence_for<Ts...>{});
      }
    }

   private:
    template <class F, size_t... Is>
    static void callBlock(F& callback, Table& table, const MatchedTable<Ts...>& matched, std::index_sequence<Is...>) {
      callback(std::span<Ts>(reinterpret_cast<Ts*>(table.column(matched.columns[Is]).data), table.count())...);
    }

    template <class F, size_t... Is>
    static void callRows(F& callback, Table& table, const MatchedTable<Ts...>& matched, uint32_t rowCount, std::index_sequence<Is...>) {
      std::tuple<Ts*...> columns{reinterpret_cast<Ts*>(table.column(matched.columns[Is]).data)...};
      const Entity* entities = table.entities().data();

      if constexpr (std::is_invocable_v<F&, Entity, Ts&...>) {
        for (uint32_t row = 0; row < rowCount; ++row)
          callback(entities[row], std::get<Is>(columns)[row]...);
      } else {
        for (uint32_t row = 0; row < rowCount; ++row)
          callback(std::get<Is>(columns)[row]...);
      }
    }

    std::vector<ComponentId> with_, without_;
    std::array<ComponentId, sizeof...(Ts)> componentIds_;
    std::vector<MatchedTable<Ts...>> matched_;
  };
}
