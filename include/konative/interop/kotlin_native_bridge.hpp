#pragma once

struct ANativeWindow;

// The flat, @CName-exported Kotlin/Native entry points the C++ core calls into
// (ARCHITECTURE.md section 6.3/section 6.4). Rendering is owned entirely by native/src/Renderer.kt -
// these three calls are the ONLY window/frame-lifecycle surface the C++ side needs, and this
// header must stay that small: if a new Kotlin-side rendering capability needs a new entry
// point, add ONE new flat @CName function + ONE new forward declaration here, never a
// C++-side GLES/EGL/Vulkan call (see render/README.md).
//
// Matches native/src/Renderer.kt's:
//   @CName("konative_render_on_window_created")   fun onWindowCreated(window: COpaquePointer?)
//   @CName("konative_render_on_window_destroyed")  fun onWindowDestroyed()
//   @CName("konative_render_tick")                 fun onTick(deltaMs: Double)
//
// SPIKE THIS FIRST (ARCHITECTURE.md section 6.3/section 9): linking a Kotlin/Native static lib into an NDK
// CMake C++ target has real, documented, unresolved community friction. Prove the link + a single
// round-trip call works before relying on this boundary for anything beyond a smoke test.
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
