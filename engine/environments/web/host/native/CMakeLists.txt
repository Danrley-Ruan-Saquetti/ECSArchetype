# OBJECT for the same reason as capi: a static library would drop the unreferenced EMSCRIPTEN_KEEPALIVE export.
oly_add_module(NAME web_host DEPENDS capi OBJECT)
