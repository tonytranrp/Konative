#pragma once

#include <cstdint>

#include "konative/core/type_traits.hpp"

namespace konative::events::window {

struct WindowResizedEvent {
    std::int32_t width = 0;
    std::int32_t height = 0;
};

static_assert(konative::core::EventType<WindowResizedEvent>);

} // namespace konative::events::window
