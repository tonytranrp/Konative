#pragma once

namespace konative::events::lifecycle {

// Fired once, after the native entry point has finished bootstrapping (World constructed,
// rendering backend not yet initialized - see WindowCreatedEvent for that).
struct AppStartedEvent {};

} // namespace konative::events::lifecycle
