#pragma once

#include <cstdint>

namespace konative::events::window {

struct WindowResizedEvent {
    std::int32_t width = 0;
    std::int32_t height = 0;
};

} // namespace konative::events::window
