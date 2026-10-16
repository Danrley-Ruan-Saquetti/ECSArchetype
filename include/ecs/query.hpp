#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <ecs/archetype.hpp>
#include <ecs/detail/small_map.hpp>
#include <ecs/type_info.hpp>
#include <memory>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

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
    virtual void onArchetypeCreated(Archetype& a) = 0;
    virtual bool matches(const Archetype& a) const = 0;
    virtual bool isMatched(const Archetype* a) const = 0;

   protected:
    uint32_t* iterating_;
  };

  namespace detail {
    // R-QY-5: mantém iterating_ > 0 enquanto um each/eachBlock está em andamento (aninhável, seguro com exceção).
    struct IterationGuard {
      explicit IterationGuard(uint32_t* p) noexcept
        : p_(p) {
        ++*p_;
      }
      ~IterationGuard() {
        --*p_;
      }
      IterationGuard(const IterationGuard&) = delete;
      IterationGuard& operator=(const IterationGuard&) = delete;
      uint32_t* p_;
    };
  } // namespace detail

  template <class... Ts>
  struct MatchedTable {
    Archetype* arch;
    std::array<uint16_t, sizeof...(Ts)> cols;
  };

  template <class... Ts>
  class Query : public QueryCacheBase {
    static_assert((!std::is_empty_v<std::remove_const_t<Ts>> && ...),
                  "tag em Query<Ts...> não é permitido; use With<Tag>/Without<Tag> (R-QY-2)");
    static_assert(((!std::is_reference_v<Ts> && !std::is_volatile_v<Ts>) && ...),
                  "Query<Ts...> aceita apenas T ou const T");

   public:
    // Metadado de leitura/escrita por tipo (R-QY-1), para uso futuro pelo scheduler.
    static constexpr std::array<bool, sizeof...(Ts)> writes{!std::is_const_v<Ts>...};

    // `with` = ids de Ts ∪ With<...>, ordenados e sem repetição; `without` idem.
    Query(uint32_t* iterating, std::vector<ComponentId> with, std::vector<ComponentId> without)
      : QueryCacheBase(iterating),
        with_(std::move(with)),
        without_(std::move(without)),
        tsIds_{componentId<std::remove_const_t<Ts>>()...} {}

    // Matching inicial. Com `smallest` percorre só os arquétipos do menor componentIndex (R-QY-3).
    void initialize(const std::vector<std::unique_ptr<Archetype>>& all, const SmallMap<ArchetypeId, uint16_t>* smallest) {
      if (smallest) {
        for (const auto& [aid, col] : *smallest)
          onArchetypeCreated(*all[aid]);
      } else {
        for (const auto& a : all)
          onArchetypeCreated(*a);
      }
    }

    size_t matchedCount() const noexcept {
      return matched_.size();
    }

    bool matches(const Archetype& a) const override {
      if (!std::includes(a.signature.begin(), a.signature.end(), with_.begin(), with_.end()))
        return false;
      for (ComponentId c : without_)
        if (std::binary_search(a.signature.begin(), a.signature.end(), c))
          return false;
      return true;
    }

    bool isMatched(const Archetype* a) const override {
      return std::any_of(matched_.begin(), matched_.end(), [a](const MatchedTable<Ts...>& m) { return m.arch == a; });
    }

    void onArchetypeCreated(Archetype& a) override {
      if (!matches(a))
        return;
      MatchedTable<Ts...> m{&a, {}};
      for (size_t i = 0; i < tsIds_.size(); ++i)
        m.cols[i] = columnOf(a, tsIds_[i]); // resolvido no match (R-QY-4)
      matched_.push_back(m);
    }

    // f(std::span<Ts>...) uma vez por tabela não vazia. Ts const → std::span<const T>.
    template <class F>
    void eachBlock(F&& f) {
      detail::IterationGuard guard(iterating_);
      for (const MatchedTable<Ts...>& m : matched_) {
        Table& t = m.arch->table;
        if (t.count() == 0)
          continue;
        callBlock(f, t, m, std::index_sequence_for<Ts...>{});
      }
    }

    // f(Ts&...) ou f(Entity, Ts&...). Os ponteiros de coluna são resolvidos antes do laço interno.
    template <class F>
    void each(F&& f) {
      detail::IterationGuard guard(iterating_);
      for (const MatchedTable<Ts...>& m : matched_) {
        Table& t = m.arch->table;
        const uint32_t n = t.count();
        if (n == 0)
          continue;
        callRows(f, t, m, n, std::index_sequence_for<Ts...>{});
      }
    }

   private:
    template <class F, size_t... Is>
    static void callBlock(F& f, Table& t, const MatchedTable<Ts...>& m, std::index_sequence<Is...>) {
      f(std::span<Ts>(reinterpret_cast<Ts*>(t.column(m.cols[Is]).data), t.count())...);
    }

    template <class F, size_t... Is>
    static void callRows(F& f, Table& t, const MatchedTable<Ts...>& m, uint32_t n, std::index_sequence<Is...>) {
      std::tuple<Ts*...> cols{reinterpret_cast<Ts*>(t.column(m.cols[Is]).data)...};
      const Entity* ents = t.entities().data();
      if constexpr (std::is_invocable_v<F&, Entity, Ts&...>) {
        for (uint32_t r = 0; r < n; ++r)
          f(ents[r], std::get<Is>(cols)[r]...);
      } else {
        for (uint32_t r = 0; r < n; ++r)
          f(std::get<Is>(cols)[r]...);
      }
    }

    std::vector<ComponentId> with_, without_;
    std::array<ComponentId, sizeof...(Ts)> tsIds_;
    std::vector<MatchedTable<Ts...>> matched_;
  };

} // namespace ecs
