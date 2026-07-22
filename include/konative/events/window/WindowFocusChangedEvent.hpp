#pragma once

#include "konative/core/type_traits.hpp"

namespace konative::events::window {

struct WindowFocusChangedEvent {
    bool has_focus = false;
};

static_assert(konative::core::EventType<WindowFocusChangedEvent>);

} // namespace konative::events::window
