#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "oly/app/context.hpp"
#include "oly/app/event.hpp"
#include "oly/core/assert.hpp"
#include "oly/core/type_name.hpp"
#include "oly/ecs/world.hpp"

namespace oly::app {
  class Engine;

  // Válido só durante a chamada do handler. Mudanças estruturais são diretas: o despacho acontece fora de
  // qualquer iteração, então a entidade citada num evento pode já ter morrido (confira com isAlive).
  class HandlerContext : public DirectContext {
   private:
    friend class Engine;

    EventTypeId eventType_;
    HandlerId handlerId_;
    std::string_view name_;
    void* userData_;
    const void* eventData_;
    uint32_t eventCount_;

    HandlerContext(Engine& engine, ecs::World& world, EventTypeId eventType, HandlerId handlerId, std::string_view name, void* userData, const void* eventData, uint32_t eventCount) noexcept
      : DirectContext(engine, world),
        eventType_(eventType),
        handlerId_(handlerId),
        name_(name),
        userData_(userData),
        eventData_(eventData),
        eventCount_(eventCount) {}

   public:
    EventTypeId eventType() const noexcept {
      return eventType_;
    }
    HandlerId handlerId() const noexcept {
      return handlerId_;
    }
    std::string_view name() const noexcept {
      return name_;
    }
    void* userData() const noexcept {
      return userData_;
    }

    // O lote do tipo do handler; não muda enquanto os handlers do tipo rodam.
    template <class T>
    std::span<const T> events() const {
      [[maybe_unused]] constexpr std::string_view typeName = oly::core::typeName<T>();
      OLY_ASSERT(eventTypeOf(eventKey<T>()) == eventType_, "events: tipo \"%.*s\" não é o do evento do handler \"%.*s\"", static_cast<int>(typeName.size()), typeName.data(), static_cast<int>(name_.size()), name_.data());

      return std::span<const T>(static_cast<const T*>(eventData_), eventCount_);
    }
    uint32_t eventCount() const noexcept {
      return eventCount_;
    }
    const void* eventData() const noexcept {
      return eventData_;
    }
  };

  using HandlerFunction = void (*)(HandlerContext& context);

  struct NamedHandler {
    HandlerFunction function;
    EventKey eventKey;
  };

  // nullptr se o nome não existe.
  const NamedHandler* findNamedHandler(std::string_view name) noexcept;

  struct NamedHandlerRegistration {
    NamedHandlerRegistration(std::string_view name, HandlerFunction function, EventKey eventKey);
  };
}

// Define `void name(HandlerContext& context, std::span<const EventType> events)` e o registra por nome.
#define OLY_HANDLER(name, EventType)                                                                                          \
  void name(oly::app::HandlerContext& context, std::span<const EventType> events);                                            \
  static void name##Adapter(oly::app::HandlerContext& context) {                                                              \
    name(context, context.events<EventType>());                                                                               \
  }                                                                                                                           \
  static const oly::app::NamedHandlerRegistration name##Registration(#name, &name##Adapter, oly::app::eventKey<EventType>()); \
  void name([[maybe_unused]] oly::app::HandlerContext& context, [[maybe_unused]] std::span<const EventType> events)
