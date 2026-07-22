#pragma once

#include <cstdint>

#include "konative/core/type_traits.hpp"

namespace konative::events::input {

struct KeyEvent {
    std::int32_t key_code = 0;
    bool is_down = false;
};

static_assert(konative::core::EventType<KeyEvent>);

} // namespace konative::events::input
