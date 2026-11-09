#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "ecs/archetype.hpp"
#include "ecs/detail/assert.hpp"
#include "ecs/detail/small_map.hpp"
#include "ecs/query.hpp"
#include "ecs/type_info.hpp"

namespace ecs {
  struct RawQuerySpec {
    std::vector<ComponentId> read{}, write{}, with{}, without{};
  };

  struct RawMatchedTable {
    Archetype* archetype;
    std::vector<uint16_t> columns; // columns[i] é a coluna de components()[i]
  };

  // Query por ComponentId, sem tipos em tempo de compilação: serve a camadas que só conhecem IDs.
  class RawQuery : public QueryCacheBase {
   private:
    std::vector<ComponentId> with_, without_, components_;
    std::vector<RawMatchedTable> matched_;
    uint32_t iterationDepth_ = 0;

   public:
    // `with` = read ∪ write ∪ with, ordenado e sem repetição; `components` = read seguido de write.
    RawQuery(uint32_t* iterating, std::vector<ComponentId> components, std::vector<ComponentId> with, std::vector<ComponentId> without)
      : QueryCacheBase(iterating),
        with_(std::move(with)),
        without_(std::move(without)),
        components_(std::move(components)) {}

    void initialize(const std::vector<std::unique_ptr<Archetype>>& all, const SmallMap<ArchetypeId, uint16_t>* smallest) {
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

    std::span<const ComponentId> components() const noexcept {
      return components_;
    }
    size_t matchedCount() const noexcept {
      return matched_.size();
    }
    std::span<const RawMatchedTable> matchedTables() const noexcept {
      return matched_;
    }
    uint32_t iterationDepth() const noexcept {
      return iterationDepth_;
    }

    bool matches(const Archetype& archetype) const override {
      return detail::signatureMatches(archetype.signature, with_, without_);
    }

    bool isMatched(const Archetype* archetype) const override {
      return std::any_of(matched_.begin(), matched_.end(), [archetype](const RawMatchedTable& matched) { return matched.archetype == archetype; });
    }

    void onArchetypeCreated(Archetype& archetype) override {
      if (!matches(archetype)) {
        return;
      }

      RawMatchedTable matched{&archetype, {}};
      matched.columns.reserve(components_.size());
      for (ComponentId componentId : components_) {
        matched.columns.push_back(columnOf(archetype, componentId));
      }

      matched_.push_back(std::move(matched));
    }

    // Marca o World como iterando até o endIteration correspondente (pode aninhar).
    void beginIteration() noexcept {
      ++*iterating_;
      ++iterationDepth_;
    }

    void endIteration() noexcept {
      ECS_ASSERT(iterationDepth_ > 0, "endIteration sem beginIteration correspondente");
      --*iterating_;
      --iterationDepth_;
    }

    // callback(Table&, std::span<const uint16_t> columns) uma vez por tabela não vazia.
    template <class F>
    void eachBlock(F&& callback) {
      detail::IterationGuard guard(iterating_);
      for (const RawMatchedTable& matched : matched_) {
        Table& table = matched.archetype->table;
        if (table.count() == 0) {
          continue;
        }

        callback(table, std::span<const uint16_t>(matched.columns));
      }
    }
  };
}
