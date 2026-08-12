#pragma once

#include "core/Error.h"
#include "core/Expected.h"

namespace mithril::core {

using Result = Expected<void, Error>;

template <typename T>
using ValueResult = Expected<T, Error>;

} // namespace mithril::core
