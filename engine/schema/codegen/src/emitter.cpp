#include "oly/schema/emitter.hpp"

#include "oly/schema/cpp_emitter.hpp"

namespace oly::schema {
  namespace {
    // Function-local, so a client's initializer can register before main regardless of the order of static
    // initialization across translation units.
    std::vector<Emitter>& registry() {
      static std::vector<Emitter> registered{Emitter{"cpp", &emitCpp}};
      return registered;
    }
  }

  bool registerEmitter(Emitter emitter) {
    registry().push_back(emitter);
    return true;
  }

  std::span<const Emitter> emitters() {
    return registry();
  }
}
