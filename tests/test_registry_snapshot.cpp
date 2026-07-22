#include <doctest/doctest.h>

#include "konative/ecs/registry_snapshot_self_check.hpp"

TEST_CASE("run_registry_snapshot_self_check: entities and components survive a save/restore round-trip") {
    CHECK(konative::ecs::run_registry_snapshot_self_check());
}
