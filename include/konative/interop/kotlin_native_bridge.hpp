#pragma once

struct ANativeWindow;

// SUPERSEDED FOR RENDERING, confirmed landed (ARCHITECTURE.md section 6.7/9) - frozen, historical,
// not extended. The flat, @CName-exported Kotlin/Native entry points the C++ core used to call into
// (ARCHITECTURE.md section 6.3/section 6.4). Rendering was historically owned entirely by
// native/src/Renderer.kt via these three calls; now it's JVM-hosted Compose (embedded_kotlin/)
// instead, with zero EGL/GLES/Vulkan anywhere in the live rendering path. If this boundary is ever
// revived for a genuinely non-rendering Kotlin/Native use, the same rule still applies: this header
// must stay small, one flat @CName function + one forward declaration per entry point, never a
// C++-side GLES/EGL/Vulkan call (see render/README.md).
//
// Matches native/src/Renderer.kt's (frozen, not currently called from anywhere live):
//   @CName("konative_render_on_window_created")   fun onWindowCreated(window: COpaquePointer?)
//   @CName("konative_render_on_window_destroyed")  fun onWindowDestroyed()
//   @CName("konative_render_tick")                 fun onTick(deltaMs: Double)
//
// The documented Kotlin/Native+NDK linking risk (ARCHITECTURE.md section 6.3/section 9) remains
// real for a possible future non-rendering use of this boundary, but is no longer gating anything
// currently shipping - "spike this first" no longer applies to today's rendering path.
extern "C" {
void konative_render_on_window_created(ANativeWindow* native_window);
void konative_render_on_window_destroyed();
void konative_render_tick(double delta_ms);
}

namespace konative::interop {

// Thin, deliberately non-adding wrappers - kept only so callers spell konative::interop::... and
// never reach for the raw extern "C" symbols directly (matches the rest of the codebase's
// namespaced-call convention).
inline void konative_render_on_window_created(ANativeWindow* native_window) {
    ::konative_render_on_window_created(native_window);
}
inline void konative_render_on_window_destroyed() { ::konative_render_on_window_destroyed(); }
inline void konative_render_tick(double delta_ms) { ::konative_render_tick(delta_ms); }

} // namespace konative::interop
