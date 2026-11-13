#include "oly/web_host/script_invoker.hpp"

#include <emscripten.h>

// Module.olyInvokeScript is installed by the @oly/core Runtime (runtime.ts) before any frame.
EM_JS(void, olyHostInvokeScript, (OlyEngine * engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount), { Module["olyInvokeScript"](engine, kind, id, events, eventCount); });

namespace {
  void invokeJavaScript([[maybe_unused]] void* userData, OlyEngine* engine, uint32_t kind, uint32_t id, const void* events, uint32_t eventCount) {
    olyHostInvokeScript(engine, kind, id, events, eventCount);
  }
}

extern "C" {
void olyWebHostInstallScriptInvoker(OlyEngine* engine) {
  olyEngineSetScriptInvoker(engine, &invokeJavaScript, nullptr);
}
}
