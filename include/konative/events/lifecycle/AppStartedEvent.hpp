#pragma once

namespace konative::events::lifecycle {

// Fired once, after the native entry point has finished bootstrapping (World constructed).
// Historically, rendering initialized later still, on WindowCreatedEvent (render/'s Kotlin/Native
// design) - that whole chain is confirmed superseded (ARCHITECTURE.md section 6.7,
// WindowCreatedEvent.hpp's own comment: no live consumer). Real rendering (JVM-hosted Compose) now
// initializes through an entirely separate chain this event has no relationship to at all
// (JNI_OnLoad -> KonativeEntryPoint.install(), ARCHITECTURE.md section 6.4) - not yet wired to
// anything real either way (see detail/lifecycle_bridge.hpp's own note, still a genuinely open
// item, not a decided design).
struct AppStartedEvent {};

} // namespace konative::events::lifecycle
