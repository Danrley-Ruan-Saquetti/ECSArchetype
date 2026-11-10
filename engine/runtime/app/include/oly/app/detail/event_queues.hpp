#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "oly/app/event.hpp"

namespace oly::app::detail {
  // Por tipo, duas filas de bytes crus no layout do struct: `pending` recebe as emissões e `dispatching` é o lote
  // da rodada em curso. Emitir durante a rodada escreve em `pending`, então o lote nunca muda nem se move.
  class EventQueues {
   private:
    struct Queue {
      std::string name;
      uint32_t size;
      std::vector<std::byte> pending;
      std::vector<std::byte> dispatching;
    };

    std::vector<Queue> queues_;
    std::vector<EventTypeId> typeByKey_; // [EventKey]

   public:
    EventTypeId declare(EventKey key, std::string_view name, uint32_t size);
    EventTypeId typeOf(EventKey key) const noexcept;
    uint32_t typeCount() const noexcept;
    std::string_view name(EventTypeId type) const;

    void push(EventTypeId type, const void* event);

    // Troca `pending` e `dispatching` dos tipos com pendência; false se não há nada pendente.
    bool beginRound();
    const std::byte* batchData(EventTypeId type) const;
    uint32_t batchCount(EventTypeId type) const;
    // Mantém a capacidade: depois do aquecimento, o despacho não aloca.
    void endRound();

    void clear();

    std::string pendingNames() const;
  };
}
