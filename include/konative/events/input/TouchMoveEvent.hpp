#pragma once

#include <cstdint>

namespace konative::events::input {

struct TouchMoveEvent {
    std::int32_t pointer_id = 0;
    float x = 0.0F;
    float y = 0.0F;
};

} // namespace konative::events::input
