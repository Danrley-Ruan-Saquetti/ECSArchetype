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

#include "oly/core/small_map.hpp"
#include "oly/ecs/component.hpp"
#include "oly/ecs/detail/archetype.hpp"
#include "oly/ecs/detail/query_cache.hpp"

namespace oly::ecs {
  class World;

  // Filtros: tags (ou componentes) que o arquétipo deve / não deve ter, sem entregá-los ao sistema.
  template <class... Us>
  struct With {};
  template <class... Us>
  struct Without {};

  // Possuída pelo World e válida enquanto ele existir. Durante each/eachBlock o World fica marcado
  // como iterando: mudança estrutural direta é erro de contrato (OLY_ASSERT) e deve passar por um
  // CommandBuffer. Spans e referências entregues ao callback só valem dentro dele.
  template <class... Ts>
  class Query : public detail::QueryCacheBase {
    static_assert((!std::is_empty_v<std::remove_const_t<Ts>> && ...),
                  "tag em Query<Ts...> não é permitido; use With<Tag>/Without<Tag> (R-QY-2)");
    static_assert(((!std::is_reference_v<Ts> && !std::is_volatile_v<Ts>) && ...),
                  "Query<Ts...> aceita apenas T ou const T");

   private:
    friend class World;

    std::vector<ComponentId> with_, without_;
    std::array<ComponentId, sizeof...(Ts)> componentIds_;
    std::vector<detail::MatchedTable<Ts...>> matched_;

   public:
    // Sem uso na v1: reservado para o scheduler (R-QY-1).
    static constexpr std::array<bool, sizeof...(Ts)> writes{!std::is_const_v<Ts>...};

    // `with` = ids de Ts ∪ With<...>, ordenados e sem repetição; `without` idem.
    Query(uint32_t* iterating, std::vector<ComponentId> with, std::vector<ComponentId> without)
      : QueryCacheBase(iterating),
        with_(std::move(with)),
        without_(std::move(without)),
        componentIds_{componentId<std::remove_const_t<Ts>>()...} {}

    size_t matchedCount() const noexcept {
      return matched_.size();
    }

    // callback(std::span<Ts>...) uma vez por tabela não vazia. Ts const → std::span<const T>.
    template <class F>
    void eachBlock(F&& callback) {
      detail::IterationGuard guard(iterating_);
      for (const detail::MatchedTable<Ts...>& matched : matched_) {
        detail::Table& table = matched.archetype->table;
        if (table.count() == 0) {
          continue;
        }

        callBlock(callback, table, matched, std::index_sequence_for<Ts...>{});
      }
    }

    // callback(Ts&...) ou callback(Entity, Ts&...).
    template <class F>
    void each(F&& callback) {
      detail::IterationGuard guard(iterating_);
      for (const detail::MatchedTable<Ts...>& matched : matched_) {
        detail::Table& table = matched.archetype->table;
        const uint32_t rowCount = table.count();
        if (rowCount == 0) {
          continue;
        }

        callRows(callback, table, matched, rowCount, std::index_sequence_for<Ts...>{});
      }
    }

   private:
    // Com `smallest`, percorre só os arquétipos do menor componentIndex (R-QY-3).
    void initialize(const std::vector<std::unique_ptr<detail::Archetype>>& all, const core::SmallMap<detail::ArchetypeId, uint16_t>* smallest) {
      if (smallest) {
        for (const auto& [archetypeId, column] : *smallest) {
          onArchetypeCreated(*all[archetypeId]);
        }
      } else {
        for (const auto& archetype : all) {
          onArchetypeCreated(*archetype);
        }
      }
    }

    bool matches(const detail::Archetype& archetype) const override {
      return detail::signatureMatches(archetype.signature, with_, without_);
    }

    bool isMatched(const detail::Archetype* archetype) const override {
      return std::any_of(matched_.begin(), matched_.end(), [archetype](const detail::MatchedTable<Ts...>& matched) { return matched.archetype == archetype; });
    }

    void onArchetypeCreated(detail::Archetype& archetype) override {
      if (!matches(archetype)) {
        return;
      }

      detail::MatchedTable<Ts...> matched{&archetype, {}};
      for (size_t i = 0; i < componentIds_.size(); ++i) {
        matched.columns[i] = detail::columnOf(archetype, componentIds_[i]);
      }

      matched_.push_back(matched);
    }

    template <class F, size_t... Is>
    static void callBlock(F& callback, detail::Table& table, const detail::MatchedTable<Ts...>& matched, std::index_sequence<Is...>) {
      callback(std::span<Ts>(reinterpret_cast<Ts*>(table.column(matched.columns[Is]).data), table.count())...);
    }

    template <class F, size_t... Is>
    static void callRows(F& callback, detail::Table& table, const detail::MatchedTable<Ts...>& matched, uint32_t rowCount, std::index_sequence<Is...>) {
      std::tuple<Ts*...> columns{reinterpret_cast<Ts*>(table.column(matched.columns[Is]).data)...};
      const Entity* entities = table.entities().data();

      if constexpr (std::is_invocable_v<F&, Entity, Ts&...>) {
        for (uint32_t row = 0; row < rowCount; ++row) {
          callback(entities[row], std::get<Is>(columns)[row]...);
        }
      } else {
        for (uint32_t row = 0; row < rowCount; ++row) {
          callback(std::get<Is>(columns)[row]...);
        }
      }
    }
  };
}
