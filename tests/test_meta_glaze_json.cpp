#include <doctest/doctest.h>

#include "konative/reflect/meta_glaze_json_self_check.hpp"

TEST_CASE("run_meta_glaze_json_self_check: an entt::meta-reflected component serializes to real, "
          "round-trippable JSON via Glaze") {
    CHECK(konative::reflect::run_meta_glaze_json_self_check());
}
