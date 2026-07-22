#include <doctest/doctest.h>

#include "konative/ecs/glm_storage_self_check.hpp"

TEST_CASE("run_glm_ecs_storage_self_check: GLM's packed vec3 round-trips correctly through EnTT's paged storage") {
    CHECK(konative::ecs::run_glm_ecs_storage_self_check());
}
