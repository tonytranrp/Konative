#pragma once

#include "konative/app/detail/lifecycle_bridge.hpp"
#include "konative/core/non_copyable.hpp"
#include "konative/ecs/world.hpp"

namespace konative::app {

// The single per-process Konative application instance - owns the World (registry + systems +
// dispatcher), reacts to platform lifecycle callbacks by dispatching the platform-agnostic
// lifecycle events. A real app derives from this and overrides on_started()/on_tick() to install
// its own systems and components.
class Application : public konative::core::NonCopyable {
public:
    virtual ~Application() = default;

    virtual void on_started() {}
    virtual void on_tick(float /*delta_seconds*/) {}
    virtual void on_paused() {}
    virtual void on_resumed() {}
    virtual void on_destroyed() {}

    void start() {
        detail::dispatch_started(world_);
        on_started();
    }

    void tick(float delta_seconds) {
        world_.tick(delta_seconds);
        on_tick(delta_seconds);
    }

    void pause() {
        detail::dispatch_paused(world_);
        on_paused();
    }

    void resume() {
        detail::dispatch_resumed(world_);
        on_resumed();
    }

    void destroy() {
        detail::dispatch_destroyed(world_);
        on_destroyed();
    }

    [[nodiscard]] konative::ecs::World& world() { return world_; }

private:
    konative::ecs::World world_;
};

} // namespace konative::app
