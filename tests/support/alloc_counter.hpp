#pragma once
#include <cstddef>

// Número de chamadas a qualquer forma de operator new desde o início do processo.
size_t allocationCount() noexcept;
