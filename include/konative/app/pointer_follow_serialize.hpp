#pragma once

#include "konative/app/pointer_follow.hpp"

// Separate opt-in serialize() header, same reasoning as spatial/transform_serialize.hpp: consumers
// that never snapshot a PointerFollow shouldn't pull cereal into their include graph just to use
// the component. cereal finds this via ADL, so it must live in PointerFollow's own namespace.
namespace konative::app {

template <class Archive>
void serialize(Archive& archive, PointerFollow& follow) {
    archive(follow.target.x, follow.target.y, follow.target.z, follow.approach_rate);
}

} // namespace konative::app
