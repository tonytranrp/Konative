#pragma once

namespace konative::events::lifecycle {

// Mirrors APP_CMD_DESTROY - the final event before the native entry point returns and the
// process's android_app glue thread tears down.
struct AppDestroyedEvent {};

} // namespace konative::events::lifecycle
