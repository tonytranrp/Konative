#include <doctest/doctest.h>

#include <sstream>

#include <cereal/archives/binary.hpp>
#include <glm/gtc/epsilon.hpp>
#include <glm/gtc/quaternion.hpp>

#include "konative/spatial/approach.hpp"
#include "konative/spatial/transform_self_check.hpp"
#include "konative/spatial/transform_serialize.hpp"

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

TEST_CASE("approach(): converges toward the target, frame-rate independently, without overshoot") {
    using konative::spatial::Transform;
    using konative::spatial::approach;
    const glm::vec3 target{100.0F, -40.0F, 12.0F};
    constexpr float kRate = 8.0F;

    // One big step vs. many small steps covering the same total time must land at exactly the
    // same point - approach.hpp's own frame-rate-independence claim, locked in as a real test
    // (e^(-r*dt) composes multiplicatively, so this is exact up to float rounding, not a tolerance
    // fudge).
    Transform one_step{};
    approach(one_step, target, kRate, 1.0F);

    Transform many_steps{};
    for (int i = 0; i < 100; ++i) {
        approach(many_steps, target, kRate, 0.01F);
    }
    CHECK(glm::all(glm::epsilonEqual(one_step.position, many_steps.position, 0.001F)));

    // Never overshoots: even an absurdly large dt lands ON the target (factor saturates at 1),
    // not past it - the exact failure mode the naive position += velocity*dt form has.
    Transform huge_step{};
    approach(huge_step, target, kRate, 1000.0F);
    CHECK(glm::all(glm::epsilonEqual(huge_step.position, target, 0.001F)));

    // And it genuinely converges: after 2 full time constants the remaining distance has dropped
    // below 14% (e^-2 ~= 0.135) of the original.
    Transform converging{};
    approach(converging, target, kRate, 2.0F / kRate);
    CHECK(glm::length(target - converging.position) < 0.14F * glm::length(target));

    // rate <= 0 or dt <= 0 means hold still, not NaN/assert - a legitimate freeze.
    Transform frozen{};
    frozen.position = glm::vec3{5.0F, 5.0F, 5.0F};
    approach(frozen, target, 0.0F, 1.0F);
    approach(frozen, target, -1.0F, 1.0F);
    approach(frozen, target, kRate, 0.0F);
    CHECK(frozen.position == glm::vec3{5.0F, 5.0F, 5.0F});
}

TEST_CASE("Transform: round-trips exactly through cereal binary serialization (transform_serialize.hpp)") {
    konative::spatial::Transform original{};
    original.position = glm::vec3{1.5F, -2.25F, 3.75F};
    original.rotation = glm::angleAxis(glm::radians(30.0F), glm::normalize(glm::vec3{1.0F, 2.0F, 3.0F}));
    original.scale = glm::vec3{0.5F, 2.0F, 4.0F};

    std::stringstream buffer;
    {
        cereal::BinaryOutputArchive output(buffer);
        output(original);
    }
    konative::spatial::Transform restored{};
    {
        cereal::BinaryInputArchive input(buffer);
        input(restored);
    }

    // Exact equality, no epsilon - binary serialization of floats is bit-preserving; anything less
    // than exact would mean a field got dropped or reordered.
    CHECK(restored.position == original.position);
    CHECK(restored.rotation.w == original.rotation.w);
    CHECK(restored.rotation.x == original.rotation.x);
    CHECK(restored.rotation.y == original.rotation.y);
    CHECK(restored.rotation.z == original.rotation.z);
    CHECK(restored.scale == original.scale);
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
