#include <doctest/doctest.h>

#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include "konative/spatial/transform_self_check.hpp"

TEST_CASE("run_transform_self_check: a real Transform round-trips through EnTT storage and "
          "to_matrix() is mathematically correct") {
    CHECK(konative::spatial::run_transform_self_check());
}

TEST_CASE("Transform: default-constructed is the identity transform") {
    konative::spatial::Transform t{};
    CHECK(t.position == glm::vec3{0.0F, 0.0F, 0.0F});
    CHECK(t.rotation.w == 1.0F);
    CHECK(t.rotation.x == 0.0F);
    CHECK(t.rotation.y == 0.0F);
    CHECK(t.rotation.z == 0.0F);
    CHECK(t.scale == glm::vec3{1.0F, 1.0F, 1.0F});
    CHECK(konative::spatial::to_matrix(t) == glm::mat4(1.0F));
}

TEST_CASE("to_matrix(): a non-trivial transform matches glm's own direct composition") {
    // Cross-checks to_matrix()'s composition against GLM's own primitives called directly, in the
    // same T * R * S order - independent confirmation beyond the hand-computed geometric cases the
    // self-check already covers.
    konative::spatial::Transform t{};
    t.position = glm::vec3{1.0F, -2.0F, 3.0F};
    t.rotation = glm::angleAxis(glm::radians(30.0F), glm::normalize(glm::vec3{1.0F, 1.0F, 0.0F}));
    t.scale = glm::vec3{2.0F, 0.5F, 1.5F};

    const glm::mat4 expected = glm::translate(glm::mat4(1.0F), t.position) *
                                glm::mat4_cast(t.rotation) *
                                glm::scale(glm::mat4(1.0F), t.scale);
    const glm::mat4 actual = konative::spatial::to_matrix(t);

    for (int col = 0; col < 4; ++col) {
        CHECK(glm::all(glm::epsilonEqual(actual[col], expected[col], 0.0001F)));
    }
}
