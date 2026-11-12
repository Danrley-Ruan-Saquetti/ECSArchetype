#pragma once

#include <cstddef>

// Number of calls to any form of operator new since the start of the process.
size_t allocationCount() noexcept;
