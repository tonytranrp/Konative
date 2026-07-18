#pragma once

namespace konative::events::lifecycle {

// Mirrors APP_CMD_PAUSE / GameActivity's onPause - the app is backgrounded but not yet destroyed.
struct AppPausedEvent {};

} // namespace konative::events::lifecycle
