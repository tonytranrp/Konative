#pragma once

#include <cstdint>

#include "konative/core/type_traits.hpp"

namespace konative::events::input {

struct TouchUpEvent {
    std::int32_t pointer_id = 0;
    float x = 0.0F;
    float y = 0.0F;
};

static_assert(konative::core::EventType<TouchUpEvent>);

} // namespace konative::events::input
