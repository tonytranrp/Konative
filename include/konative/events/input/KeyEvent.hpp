#pragma once

#include <cstdint>

namespace konative::events::input {

struct KeyEvent {
    std::int32_t key_code = 0;
    bool is_down = false;
};

} // namespace konative::events::input
