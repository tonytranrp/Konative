#pragma once

#include <functional>
#include <utility>
#include <vector>

#include "konative/ecs/detail/system_traits.hpp"
#include "konative/ecs/registry.hpp"

namespace konative::ecs {

// An ordered list of systems run once per frame. Parallel/cross-system-dependency scheduling
// (Taskflow) is layered on top of this in konative::scheduling, not folded into System itself -
// keep this the single-threaded, easy-to-reason-about default.
class SystemGraph {
public:
    template <detail::SystemLike F>
    void add(F&& system) {
        systems_.emplace_back(std::forward<F>(system));
    }

    void run(Registry& registry, float delta_seconds) {
        for (auto& system : systems_) {
            system(registry, delta_seconds);
        }
    }

private:
    std::vector<std::function<void(Registry&, float)>> systems_;
};

} // namespace konative::ecs
