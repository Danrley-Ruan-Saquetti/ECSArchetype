#pragma once

#include "oly/capi/c_api.hpp"
#include "oly/capi/engine_api.hpp"

extern "C" {
// Installs on `engine` the invoker that calls Module.olyInvokeScript(engine, kind, id, events, eventCount); that JS
// function must exist before the first script call. The TS client calls it right after olyEngineCreate.
OLY_API void olyWebHostInstallScriptInvoker(OlyEngine* engine);
}
