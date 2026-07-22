#pragma once

#include <cstdint>

#include "konative/core/type_traits.hpp"

namespace konative::events::input {

struct TouchDownEvent {
    std::int32_t pointer_id = 0;
    float x = 0.0F;
    float y = 0.0F;
};

// events/README.md's own Hard Rule ("every event must satisfy konative::core::EventType") was
// previously enforced by convention only, never actually checked by the type system - this makes
// it a real compile-time check.
static_assert(konative::core::EventType<TouchDownEvent>);

} // namespace konative::events::input
