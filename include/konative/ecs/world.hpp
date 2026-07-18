#pragma once

#include "konative/core/non_copyable.hpp"
#include "konative/ecs/registry.hpp"
#include "konative/ecs/system.hpp"
#include "konative/events/dispatcher.hpp"

namespace konative::ecs {

// The composition root for a running Konative app instance: one Registry, one SystemGraph, one
// event Dispatcher. entt::registry::ctx() is Konative's dependency-injection mechanism
// (ARCHITECTURE.md \xc2\xa74 - deliberately not a separate DI library) - register cross-cutting services
// via registry().ctx().emplace<Service>(...).
class World : public konative::core::NonCopyable {
public:
    [[nodiscard]] Registry& registry() { return registry_; }
    [[nodiscard]] SystemGraph& systems() { return systems_; }
    [[nodiscard]] konative::events::Dispatcher& events() { return dispatcher_; }

    void tick(float delta_seconds) {
        systems_.run(registry_, delta_seconds);
        dispatcher_.update();
    }

private:
    Registry registry_;
    SystemGraph systems_;
    konative::events::Dispatcher dispatcher_;
};

} // namespace konative::ecs
