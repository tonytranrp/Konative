#pragma once

#include "konative/app/heartbeat_counter.hpp"

// Separate opt-in serialize() header, same reasoning as pointer_follow_serialize.hpp/
// spatial/transform_serialize.hpp: consumers that never snapshot a HeartbeatCounter shouldn't
// pull cereal into their include graph just to use the component. cereal finds this via ADL, so
// it must live in HeartbeatCounter's own namespace.
namespace konative::app {

template <class Archive>
void serialize(Archive& archive, HeartbeatCounter& counter) {
    archive(counter.ticks);
}

} // namespace konative::app
