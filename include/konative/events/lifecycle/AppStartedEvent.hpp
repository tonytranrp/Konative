#pragma once

namespace konative::events::lifecycle {

// Fired once, after the native entry point has finished bootstrapping (World constructed).
// Historically, rendering initialized later still, on WindowCreatedEvent (render/'s Kotlin/Native
// design) - that whole chain is confirmed superseded (ARCHITECTURE.md section 6.7,
// WindowCreatedEvent.hpp's own comment: no live consumer). Real rendering (JVM-hosted Compose)
// initializes through an entirely separate chain this event has no relationship to
// (JNI_OnLoad -> KonativeEntryPoint.install(), ARCHITECTURE.md section 6.4) - but THIS event is
// real and wired, since 2026-07-21: detail/lifecycle_bridge.hpp's dispatch_started() triggers it
// from Application::start(), itself called by src/platform/android/jni_onload.cpp's
// native_dispatch_lifecycle_event() on the real Activity's onActivityCreated, via RegisterNatives.
struct AppStartedEvent {};

} // namespace konative::events::lifecycle
