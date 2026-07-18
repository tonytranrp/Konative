package konative.render

import kotlin.experimental.ExperimentalNativeApi
import kotlin.native.CName
import kotlinx.cinterop.COpaquePointer
import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.reinterpret
import platform.android.ANativeWindow
import platform.egl.EGLContext
import platform.egl.EGLSurface
import platform.egl.EGL_DEFAULT_DISPLAY
import platform.egl.eglDestroyContext
import platform.egl.eglDestroySurface
import platform.egl.eglGetDisplay
import platform.egl.eglInitialize
import platform.egl.eglSwapBuffers
import platform.egl.eglTerminate
import platform.gles3.GL_COLOR_BUFFER_BIT
import platform.gles3.glClear
import platform.gles3.glClearColor

// Owns the EGL context/surface + all GLES calls for the entire framework (ARCHITECTURE.md section 6.2).
// include/konative/render/renderer.hpp (C++) only forwards window/tick events to the three
// @CName functions below across the interop boundary - no EGL/GLES/Vulkan call may exist
// anywhere on the C++ side (render/README.md, native/README.md).
//
// Kotlin/Native ships EGL + GLES2/GLES3 cinterop bindings for every androidNative* target OUT OF
// THE BOX (platform.egl / platform.gles2 / platform.gles3 / platform.android,
// kotlin-native/platformLibs/src/platform/android/*.def in the JetBrains/kotlin source tree) -
// no custom .def file is needed for GLES rendering (see native/cinterop/README.md, which is why
// this file has no cinterop/ counterpart). The real prior art this is modeled on is
// natario1/Egloo (github.com/natario1/Egloo), a published, maintained Kotlin Multiplatform
// library that binds EGL+GLES for androidNative* targets exactly this way - its
// library/src/androidNativeMain/kotlin/.../internal/egl.kt is the reference implementation to
// port the eglChooseConfig/eglCreateContext/eglCreateWindowSurface sequence from below.
//
// UNPROVEN (ARCHITECTURE.md section 9): this is the framework's single largest concentration of risk.
// Egloo proves the EGL/GLES cinterop bindings compile and link for Android native targets: it
// does NOT itself prove a JVM-free, NativeActivity-driven render loop end to end on a real
// device - that validation is this project's own to do, via testapp/'s adb verification loop.

@OptIn(ExperimentalForeignApi::class)
private object EglState {
    var display: platform.egl.EGLDisplay? = null
    var surface: EGLSurface? = null
    var context: EGLContext? = null
}

@OptIn(ExperimentalForeignApi::class, ExperimentalNativeApi::class)
@CName("konative_render_on_window_created")
fun onWindowCreated(window: COpaquePointer?) {
    val nativeWindow = window?.reinterpret<ANativeWindow>() ?: return

    val display = eglGetDisplay(EGL_DEFAULT_DISPLAY)
    eglInitialize(display, null, null) // major/minor may be null per the EGL spec - we don't need the version.
    EglState.display = display

    // TODO(spike, ARCHITECTURE.md section 9): eglChooseConfig -> eglCreateContext ->
    // eglCreateWindowSurface(display, config, nativeWindow as platform.egl.EGLNativeWindowType,
    // ...) -> eglMakeCurrent. Deliberately left unimplemented rather than guessed at: port this
    // sequence from Egloo's real, working egl.kt/EglCore.kt (see this file's own top comment for
    // the exact path) instead of inventing the EGLConfig array/cinterop-type handling from
    // scratch. eglGetDisplay/eglInitialize above are the two calls this file is currently
    // confident about; nativeWindow is unused until the surface-creation call is filled in.
}

@OptIn(ExperimentalForeignApi::class, ExperimentalNativeApi::class)
@CName("konative_render_on_window_destroyed")
fun onWindowDestroyed() {
    val display = EglState.display ?: return
    EglState.surface?.let { eglDestroySurface(display, it) }
    EglState.context?.let { eglDestroyContext(display, it) }
    eglTerminate(display)
    EglState.display = null
    EglState.surface = null
    EglState.context = null
}

@OptIn(ExperimentalForeignApi::class, ExperimentalNativeApi::class)
@CName("konative_render_tick")
fun onTick(deltaMs: Double) {
    val display = EglState.display ?: return
    val surface = EglState.surface ?: return // no-op until onWindowCreated's TODO is filled in.
    glClearColor(0.05f, 0.05f, 0.08f, 1.0f)
    glClear(GL_COLOR_BUFFER_BIT)
    eglSwapBuffers(display, surface)
}
