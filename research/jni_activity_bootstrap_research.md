# Konative — JNI/Reflection Activity-Bootstrap Research

Scope: the *next* step after `research/research.md` §1.1 (which already covers, and this report does
not re-derive: `ActivityThread.currentApplication()`, `InMemoryDexClassLoader` construction, and why
`env->FindClass()` can't see dex-loaded classes so `ClassLoader.loadClass()` reflection is used
instead). This report starts from a live `Application` jobject and answers: how does a `.so` whose
*only* trigger is `System.loadLibrary()` (no Activity, no Context, no Java code beyond
`MainActivity.kt`'s trivial loader) end up with a live `Activity` reference and a working Compose UI
as that Activity's content — with zero `.kt`/`.java` source files in `testapp/` beyond
`MainActivity.kt` itself.

Local files read for grounding: [`testapp/app/src/main/java/com/konative/testapp/MainActivity.kt`](../testapp/app/src/main/java/com/konative/testapp/MainActivity.kt),
[`testapp/app/src/main/AndroidManifest.xml`](../testapp/app/src/main/AndroidManifest.xml),
[`testapp/README.md`](../testapp/README.md), [`ARCHITECTURE.md`](../ARCHITECTURE.md) (esp. the
2026-07-17 status banner and §6), [`research/research.md`](research.md) §1.1–1.2,
[`../../GameHub/testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java`](../../GameHub/testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java),
[`../../GameHub/libs/jni/src/dex_loader.cpp`](../../GameHub/libs/jni/src/dex_loader.cpp),
[`../../GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp`](../../GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp),
and Konative's own (currently stale — see §5) `src/platform/android/activity_bridge.cpp`,
`include/konative/platform/android/README.md`, `include/konative/app/entry_point.hpp`.

---

## 1. Getting the `Application` singleton, and its hidden-API standing (API 26–36)

### 1.1 The exact JNI call sequence (matches `GameHub`'s already-working code verbatim)

```cpp
jclass activity_thread_class = env->FindClass("android/app/ActivityThread");
jmethodID current_application = env->GetStaticMethodID(
    activity_thread_class, "currentApplication", "()Landroid/app/Application;");
jobject application = env->CallStaticObjectMethod(activity_thread_class, current_application);
```

This is exactly [`GameHub/libs/jni/src/dex_loader.cpp`](../../GameHub/libs/jni/src/dex_loader.cpp)
lines 19–29's own sequence. Nothing about this changes for Konative.

### 1.2 Hidden-API status — verified by directly grepping Google's own published flag lists, not inference

Android's non-SDK interface policy (current terminology, superseding the old
whitelist/greylist/dark-greylist/blacklist names) classifies every reflectively-reachable member into
one of: `sdk`/`public-api` (fully supported), `unsupported` (a.k.a. the old "light greylist" —
**usable regardless of target API level, indefinitely, with no stability guarantee**), `max-target-X`
(the old "dark greylist" — blocked once the *calling app's* `targetSdkVersion` is ≥ the Android
version `X` denotes), or `blocked` (never usable via reflection/JNI by a normal app, any target SDK).
Source: [Restrictions on non-SDK interfaces](https://developer.android.com/guide/app-compatibility/restrictions-non-sdk-interfaces).

I downloaded Google's own published per-release `hiddenapi-flags.csv` (the same file the docs page
links: `https://dl.google.com/developers/android/<codename>/non-sdk/hiddenapi-flags.csv`) for two
releases and grepped both directly:

| Symbol | Android 10 / Q (API 29) flag | Latest available / Baklava (API 36 range — matches Konative's own test device) flag |
|---|---|---|
| `Landroid/app/ActivityThread;->currentApplication()Landroid/app/Application;` | `greylist` | `unsupported` |
| `Landroid/app/ActivityThread;->currentActivityThread()Landroid/app/ActivityThread;` | `greylist` | `unsupported` |

(Downloaded from `https://dl.google.com/developers/android/qt/non-sdk/hiddenapi-flags.csv` and
`https://dl.google.com/developers/android/baklava/non-sdk/hiddenapi-flags.csv` — the second URL is
the exact download link `developer.android.com/about/versions/16/changes/non-sdk-16` itself provides
for the current release. Grepped locally; both lines are single-flag entries, i.e. **not** also
tagged `sdk`/`max-target-X`/`blocked`.)

**Conclusion**: `currentApplication()` has sat in the same permissive, indefinitely-usable tier
continuously from API 29 through the current release (just renamed `greylist`→`unsupported` along
the way, per [Android 10's own migration notes](https://developer.android.com/about/versions/10/non-sdk-q)).
It has never been moved to a `max-target-X` or `blocked` tier in this span. This is a materially
stronger answer than "many blog posts/libraries use it and it seems fine" — it's a direct read of
Google's own enforcement data for the exact API range Konative targets.

### 1.3 Manifest/production opt-out — there isn't one, and none is needed here

Per the same docs page: the only levers that widen non-SDK access
(`adb shell settings put global hidden_api_policy 1/0`, or `android:debuggable`+instrumentation) are
**development-device-only** and have no effect on a signed, non-debuggable production APK — there is
no manifest meta-data equivalent to `--allow-hidden-api` for shipped apps. This doesn't matter for
Konative because `currentApplication()`/`currentActivityThread()` are `unsupported`, not
`max-target-X`/`blocked` — the policy's opt-out mechanisms are irrelevant to a call that was never
restricted for production apps in the first place. **The one defensive thing worth keeping**
(already present in `GameHub`'s own code) is null-checking the returned `Application` and logging
rather than crashing — not because of hidden-API enforcement, but because `currentApplication()`
can legitimately return `null` if called too early in process bring-up.

### 1.4 A structural guarantee, independent of Android internals or hidden-API policy entirely

`JNI_OnLoad` fires because `System.loadLibrary()` runs inside `MainActivity`'s companion-object
`init {}` block — which Kotlin compiles into `MainActivity`'s `<clinit>` static initializer. The
JLS/JVM specification guarantees a class's static initializer completes **before any instance of
that class can be constructed**, and `Activity.onCreate()` cannot be called on an instance that
doesn't exist yet. So — independent of any Android-version-specific `ActivityThread` implementation
detail — `JNI_OnLoad` is *structurally* guaranteed to finish before `MainActivity.onCreate()` (or
`attach()`, or anything else instance-level) can run. This is the load-bearing timing fact for §2/§3
below, and it doesn't depend on hidden API policy or on Android internals remaining stable.

---

## 2. Registering `Application.ActivityLifecycleCallbacks` from native code — Proxy vs. a real compiled class

### 2.1 Can `java.lang.reflect.Proxy` be reached from JNI at all? Yes.

`Proxy.newProxyInstance(ClassLoader, Class<?>[], InvocationHandler)` is ordinary public JVM API,
reachable via plain JNI reflection (`FindClass("java/lang/reflect/Proxy")` +
`GetStaticMethodID(..., "newProxyInstance", "(Ljava/lang/ClassLoader;[Ljava/lang/Class;Ljava/lang/reflect/InvocationHandler;)Ljava/lang/Object;")`
+ `CallStaticObjectMethod`). On Android specifically, `Proxy.newProxyInstance` synthesizes the
*proxy class itself* as DEX bytecode at the call site — it does not need the interface's proxy
implementation to be pre-compiled. Source (Android-specific behavior confirmed):
["Dynamic Code Execution: Proxies and Class Loading in Java and Android"](https://medium.com/@aghajari/dynamic-code-execution-proxies-and-class-loading-in-java-and-android-97134d10be0c).
Real, working native-code precedent for exactly this call from JNI: `android-jni-bridge`'s
[`Proxy.cpp`](https://github.com/bitter/android-jni-bridge/blob/master/Proxy.cpp) (used by Unity's
own Android JNI bridge — [Unity-Technologies fork](https://github.com/Unity-Technologies/android-jni-bridge/blob/master/Proxy.cpp)),
which calls a static `newInterfaceProxy(J[Ljava/lang/Class;)Ljava/lang/Object;` helper and wires a
native `invoke()` handler.

### 2.2 But the `InvocationHandler` argument is where this stops being reflection-only

`Proxy.newProxyInstance`'s third argument must be a **live instance** of a class implementing
`InvocationHandler`. An instance requires a class, and Android's JNI **cannot define a class from
raw bytes at runtime**: `env->DefineClass()` is present in the vtable but is a documented no-op on
Android — "Android does not use Java bytecodes or class files, so passing in binary class data
doesn't work... DefineClass is not supported by the Android runtime," per the
[NDK's own JNI tips guide](https://developer.android.com/ndk/guides/jni-tips). So the
`InvocationHandler` implementation must already exist as a *loaded* class — which, given `testapp/`
has no second `.kt` file, can only come from the embedded dex blob. `android-jni-bridge`'s own
`Proxy.cpp` confirms this in practice: its `InterfaceProxy` invocation-handler class is a real,
pre-compiled Java class shipped with that library, not something synthesized purely from C++.

### 2.3 Therefore: Proxy buys nothing here, and direct implementation is strictly simpler

Since a real compiled class must live in the embedded dex *either way*, the two options are:

- **(a) Proxy path**: embed a generic `InvocationHandler` class (with a native `invoke(Object proxy, Method method, Object[] args)` callback) in the dex, call `Proxy.newProxyInstance` from native code with `Application.ActivityLifecycleCallbacks.class` as the sole interface, and have the native `invoke()` implementation `switch` on `method.getName()` to figure out which of the 7 callback methods fired, unboxing `args[0]`/`args[1]` by hand.
- **(b) Direct path**: embed one small Kotlin class that `: Application.ActivityLifecycleCallbacks` directly, with 7 ordinary `override fun`s.

(b) has zero generic dispatch, zero `Method`/`Object[]` marshaling, full compile-time type safety, and is not meaningfully harder to write than (a)'s `InvocationHandler`. **Proxy is a strictly worse tool here** — it exists to implement an interface *without* a pre-written class, but that precondition (no class available anywhere) is exactly what's false the moment an embedded dex exists at all. Recommendation: skip `java.lang.reflect.Proxy` entirely for this use case.

### 2.4 The "one `.kt` file in testapp" constraint does not reach the embedded dex — explicitly, why

Two independent, non-overlapping build pipelines exist:

1. **`testapp/`'s own Gradle/AGP `kotlinc` pass** — compiles literally one file
   (`MainActivity.kt`) into `testapp`'s own `classes.dex`, per
   [`testapp/README.md`](../testapp/README.md)'s own hard rule ("The Kotlin Android Gradle plugin...
   exists ONLY to compile this one loader file... must never be used to compile the real Compose UI
   code").
2. **Konative's own CMake-driven `kotlinc`+`d8` pipeline** (mirroring
   [`GameHub/cmake/modules/JvmDex.cmake`](../../GameHub/cmake/modules/JvmDex.cmake), per
   `research/research.md` §1.2 and the `ARCHITECTURE.md` 2026-07-17 status banner) — compiles
   Kotlin source living *outside* `testapp/` entirely (e.g. a new `native/kotlin-dex/` tree,
   parallel to the existing `native/src/` Kotlin/Native tree) into a **separate** `classes.dex`,
   which is embedded as linked byte data inside `libkonative_app_native.so` and never touches
   `testapp/`'s Gradle build at all.

The embedded dex's classes never exist as `.kt` files anywhere `testapp/`'s own build can see them —
from `testapp/`'s and its README rule's perspective they are opaque bytes inside a prebuilt `.so`.
**The "exactly one `.kt` file" rule is a rule about `testapp/`'s own Gradle compilation unit, not a
global ceiling on Kotlin source anywhere in the repo.** The embedded dex can and should contain: the
`ActivityLifecycleCallbacks` implementation, the `LifecycleOwner`/`ViewModelStoreOwner`/
`SavedStateRegistryOwner` holder, the `@Composable` UI tree, and one `@JvmStatic` entry-point object
— all real Kotlin, compiled by a pipeline `testapp/` never invokes.

**A build-time corollary worth flagging explicitly** (not part of the original 4 questions, but
directly relevant to "what goes in the dex"): unlike `GameHub`'s own `JvmDex.cmake` use cases (real
`Dialog`/`View`/`WindowManager` overlays — pure Android-framework SDK, resolvable through
`application.getClassLoader()`'s parent chain with nothing extra needed), **Jetpack Compose is never
part of the Android OS image** — `androidx.compose.*`/`androidx.lifecycle.*`/`androidx.savedstate.*`
are ordinary app-bundled library jars. Since `testapp/`'s own Gradle build deliberately excludes
Compose dependencies (README rule above), the embedded dex's own `kotlinc -cp .../d8` step must
include the actual `compose-runtime`/`compose-ui`/`lifecycle-runtime`/`lifecycle-viewmodel`/
`savedstate` jars (extracted from their AARs) on its classpath and in its dex input, alongside
Konative's own hand-written Kotlin — not just Konative's own sources the way `GameHub`'s simpler
Dialog-based UI could get away with. This very likely pushes the embedded blob's unshrunk size well
past `research/research.md` §8's already-cited "~2.5MB for a near-trivial Kotlin object" number
(Compose's own dependency graph is substantially larger than bare `kotlin-stdlib`) — genuinely
unmeasured here, flagged as a real open risk rather than assumed solved.

---

## 3. From a live `Activity` to a rendered Compose UI — the C++/Kotlin handoff

### 3.1 Why a plain `Activity` needs manual `ViewTree*Owner` wiring at all

`ComposeView`/`AbstractComposeView.setContent{}` throws "Composed into the View which doesn't
propagate ViewTreeLifecycleOwner!" unless a `LifecycleOwner` **and** `SavedStateRegistryOwner` are
reachable by walking up the view tree — normally supplied automatically by `ComponentActivity`
(`AppCompatActivity`/`FragmentActivity` included), never by plain `android.app.Activity`. Sources:
[JetBrains/compose-multiplatform#3203](https://github.com/JetBrains/compose-multiplatform/issues/3203),
[AndroidBugFix write-up](https://www.androidbugfix.com/2022/08/inputmethodservice-with-jetpack-compose.html).
A `ViewModelStoreOwner` is additionally required *lazily*, only if a composable actually calls
`viewModel()` — cheap enough to wire up front regardless.

### 3.2 The manual wiring pattern (real, working, used by overlay/service/IME Compose hosts)

Confirmed via direct fetch of [helw.net — "Compose UI without an Activity"](https://helw.net/2025/08/31/compose-ui-without-an-activity/)
plus the WindowManager-overlay/Service-hosted-Compose family of write-ups
([Medium — Compose in WindowManager overlays](https://medium.com/@rohitkasanwal/using-jetpack-compose-in-windowmanager-overlays-a-case-study-a8aea57cb44d),
[techyourchance — Compose inside a Service](https://www.techyourchance.com/jetpack-compose-inside-android-service/),
[gist — Compose overlay Service](https://gist.github.com/handstandsam/6ecff2f39da72c0b38c07aa80bbb5a2f)):

1. A small class holds a `LifecycleRegistry`, a `ViewModelStore`, and a
   `SavedStateRegistryController.create(this)`, implementing `LifecycleOwner`+`ViewModelStoreOwner`+
   `SavedStateRegistryOwner`.
2. `savedStateRegistryController.performRestore(null)` **before** any lifecycle event is dispatched
   (Compose's saved-state machinery requires the registry past `CREATED` before
   `consumeRestoredStateForKey` is legal).
3. Drive `lifecycleRegistry.handleLifecycleEvent(...)` through `ON_CREATE` → `ON_START` → `ON_RESUME`
   — **`ON_START` is not optional**: Compose's recomposer only actually composes/renders once the
   lifecycle reaches at least `STARTED` (per [Lifecycle in Jetpack Compose](https://developer.android.com/topic/libraries/architecture/lifecycle),
   "Compose waits until the app is visible to render the changes" — state can be mutated earlier,
   but nothing draws until `STARTED`+).
4. On the `ComposeView`: `view.setViewTreeLifecycleOwner(owner)`,
   `view.setViewTreeViewModelStoreOwner(owner)`, `view.setViewTreeSavedStateRegistryOwner(owner)` —
   current (non-deprecated) forms are **Kotlin extension functions** on `View`
   (`androidx.lifecycle.setViewTreeLifecycleOwner`/`setViewTreeViewModelStoreOwner`,
   `androidx.savedstate.setViewTreeSavedStateRegistryOwner`), replacing the old
   `ViewTreeLifecycleOwner.set(view, owner)` static-method form. Source:
   [androidx.lifecycle release notes](https://developer.android.com/jetpack/androidx/releases/lifecycle)
   ("ViewTreeLifecycleOwner is now written in Kotlin, and you must now directly import and use the
   Kotlin extension methods... This replaces the previous Kotlin extension in lifecycle-runtime-ktx").
5. `activity.setContentView(composeView)`.

**Because our own `ActivityLifecycleCallbacks` implementation (§2) already receives every one of
`onActivityCreated`/`onActivityStarted`/`onActivityResumed`/`onActivityPaused`/`onActivityStopped`/
`onActivityDestroyed` for the real Activity, the cleanest design drives the fake `LifecycleRegistry`
directly off those real callbacks** (`onActivityStarted` → `ON_START`, etc.) rather than firing
`ON_CREATE`→`ON_RESUME` all at once — this keeps Compose's own internal state (animation
pause/resume, `DisposableEffect` timing) genuinely tracking the real Activity, not a fabrication.

### 3.3 Where should this logic live — C++/JNI reflection, or Kotlin?

**Kotlin, entirely, past one handoff point.** Once native code holds a `jobject application` (§1)
and has `loadClass()`'d one entry-point class out of the embedded dex (§2.4's mechanism, identical to
[`GameHub/libs/jni/src/dex_loader.cpp`](../../GameHub/libs/jni/src/dex_loader.cpp)'s already-proven
steps 2–5), the *only* remaining native-side call is one ordinary static-method invocation — no
further reflection is needed for anything Compose-specific, because from that point on real Kotlin
code is running as real loaded JVM bytecode with normal typed access to
`Application.registerActivityLifecycleCallbacks` (itself fully public SDK, zero hidden-API surface —
**every hidden-API touchpoint in this entire design is confined to §1's one call**), `ComposeView`,
`setContent`, and `Activity.setContentView`. Reasoning for favoring this split: JNI reflection from
C++ for framework-adjacent, frequently-changing library surface (Compose's own APIs, which are far
less stable than `Application.ActivityLifecycleCallbacks`) is exactly the kind of fragile,
hard-to-refactor code this project should minimize; keeping it in Kotlin means ordinary Kotlin
compiler type-checking catches breakage at Konative's *own* build time instead of at runtime on a
user's device.

---

## 4. Prior art for "Java side is only `loadLibrary()`, native code drives Activity content"

No project found does *exactly* this (native-triggered `ActivityLifecycleCallbacks` registration +
runtime `ViewTree*Owner` fabrication + Compose from a dex loaded by the app's own `JNI_OnLoad`).
Closest adjacent precedents, each missing a different piece:

- **`android.app.NativeActivity`** (framework-provided, [API reference](https://developer.android.com/reference/android/app/NativeActivity)) — Google's own "native code drives Activity content" mechanism, and the closest **official** precedent. Its `ANativeActivity` struct hands native code the Activity's `jobject` **directly** (field historically named `clazz`, actually "a reference to the... object created by the Android framework," per [utzcoz's NativeActivity analysis](https://utzcoz.github.io/2021/12/25/Analyze-NativeActivity.html)) — no `ActivityThread`/reflection dance needed at all. But it (a) requires the manifest to declare `NativeActivity` (or a trivial subclass) rather than an arbitrary custom `Activity`, and (b) takes over the **entire window as a raw `Surface`** — it has no support for `setContentView(arbitraryView)`, so it cannot host a Compose `View` tree at all. This is also *not* what Konative's own current `testapp/` uses — `AndroidManifest.xml`/`MainActivity.kt` deliberately moved off `NativeActivity` specifically because Compose needs a real View-hosting Activity (see `ARCHITECTURE.md`'s 2026-07-17 banner) — worth naming explicitly since `ARCHITECTURE.md` §6.1/§13's still-unrewritten body describes the superseded `NativeActivity` design.
- **Xamarin/`dotnet/android`'s `MonoPackageManager`** — [`MonoPackageManager.java`](https://github.com/xamarin/xamarin-android/blob/main/src/java-runtime/java/mono/android/MonoPackageManager.java) loads native libs, initializes the Mono runtime, and loads managed assemblies, triggered from `attachInfo()` — a `ContentProvider.attachInfo()` hook, which runs *even earlier* than any Activity's class-loading (before `Application.onCreate()`). It still requires a **generated** Java class (`ApplicationRegistration.java`) and a manifest-registered `ContentProvider` — i.e. real non-trivial Java source, just tool-generated rather than hand-written. Konative's `System.loadLibrary()`-in-companion-object approach achieves comparably-early execution (§1.4) without needing a second manifest-registered component at all.
- **Flutter's `FlutterActivity`** — architecturally the *opposite* direction: `FlutterActivity.onCreate()` (real, substantial Kotlin/Java, not a one-liner) calls `delegate.onAttach(this)` (which does `System.loadLibrary("flutter")` internally) and *then* `setContentView(createFlutterView())` — the **Java side drives the native engine**, not the reverse. Source: [FlutterActivity javadoc](https://api.flutter.dev/javadoc/io/flutter/embedding/android/FlutterActivity.html), [Flutter engine boot walkthrough](https://xiongcen.github.io/2020/09/01/flutter-booting/).
- **Frida** (dynamic instrumentation) achieves "drive Activity lifecycle/UI from outside normal app Java code" via a *separately attached* instrumentation runtime/process, not the app's own `JNI_OnLoad` — a related technique family, not a real match. Source: [Frida Java bridge / Android hooking examples](https://codeshare.frida.re/@salecharohit/instrumenting-native-android-functions-using-frida/).
- **Godot/Unity Android ports** ship real, non-trivial `GodotActivity`/`UnityPlayerActivity` Java subclasses — same contrast already established in `research/research.md` §5, reconfirmed here, not re-researched.

**Conclusion**: Konative's exact target shape (arbitrary custom `Activity`, zero Java/Kotlin beyond
`loadLibrary()`, native-triggered Compose content) appears to be a genuine, un-precedented
combination — closest in *spirit* to `NativeActivity` (official, native-drives-content) crossed with
`GameHub`'s own dex-embedding mechanism (already-proven in this workspace), but the specific
"`ActivityLifecycleCallbacks`-to-obtain-the-Activity, then fabricate `ViewTree*Owner`s for Compose"
bridge connecting them does not appear to exist anywhere as prior art.

---

## 5. Concrete recommended design

**Handoff point**: native code's job ends at exactly one `CallStaticVoidMethod` invocation. Every
Compose-specific concern lives in Kotlin, in the embedded dex.

### 5.1 Native (`JNI_OnLoad`, C++ — replaces this project's stale NativeActivity-era entry point)

`src/platform/android/activity_bridge.cpp`/`looper_pump.cpp`/`android_main.cpp` and
`include/konative/platform/android/*` (per their own README) are `android_native_app_glue`/
`ANativeActivity_onCreate`-shaped — **none of that applies to this design**; there is no
`ANativeWindow` handoff and no native event-loop entry point anymore. The real entry point is a
plain `JNI_OnLoad(JavaVM*, void*)`, analogous to `GameHub`'s loader but new to this codebase.

```cpp
jint JNI_OnLoad(JavaVM* vm, void*) {
    JNIEnv* env; vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);

    // 1. ActivityThread.currentApplication() — §1. The ONE hidden-API call in this whole design.
    jclass at = env->FindClass("android/app/ActivityThread");
    jmethodID m = env->GetStaticMethodID(at, "currentApplication", "()Landroid/app/Application;");
    jobject application = env->CallStaticObjectMethod(at, m);
    if (!application) { /* log "too early", bail — GameHub's existing pattern */ }

    // 2-5. Reuse GameHub's load_class_from_dex() verbatim (getClassLoader() -> NewDirectByteBuffer
    // -> InMemoryDexClassLoader -> loadClass("com.konative.generated.KonativeEntryPoint")) — §2.4.
    auto loaded = konative::jni::load_class_from_dex(env, kEmbeddedDexBytes, kEmbeddedDexSize,
                                                       "com.konative.generated.KonativeEntryPoint");

    // 6. ONE static call. Handoff ends here — everything past this point is Kotlin.
    jmethodID install = env->GetStaticMethodID(loaded.clazz.get(), "install",
                                                 "(Landroid/app/Application;)V");
    env->CallStaticVoidMethod(loaded.clazz.get(), install, application);
    return JNI_VERSION_1_6;
}
```

### 5.2 Embedded dex Kotlin (compiled by Konative's own `kotlinc`+`d8` CMake pipeline, §2.4 — never in `testapp/`)

```kotlin
package com.konative.generated

object KonativeEntryPoint {
    @JvmStatic
    fun install(application: Application) {
        application.registerActivityLifecycleCallbacks(object : Application.ActivityLifecycleCallbacks {
            private var owner: ComposeHostOwner? = null

            override fun onActivityCreated(activity: Activity, saved: Bundle?) {
                if (owner != null) return                 // only the first Activity matters here
                owner = ComposeHostOwner().apply { registry.performRestore(saved) }
                owner!!.registry.handleLifecycleEvent(Lifecycle.Event.ON_CREATE)

                val composeView = ComposeView(activity).apply {
                    setViewTreeLifecycleOwner(owner)
                    setViewTreeViewModelStoreOwner(owner)
                    setViewTreeSavedStateRegistryOwner(owner)
                    setContent { KonativeRootComposable() }   // real Compose, real Kotlin
                }
                activity.setContentView(composeView)
            }
            override fun onActivityStarted(a: Activity) { owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_START) }
            override fun onActivityResumed(a: Activity)  { owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_RESUME) }
            override fun onActivityPaused(a: Activity)   { owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_PAUSE) }
            override fun onActivityStopped(a: Activity)  { owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_STOP) }
            override fun onActivityDestroyed(a: Activity){ owner?.registry?.handleLifecycleEvent(Lifecycle.Event.ON_DESTROY) }
            override fun onActivitySaveInstanceState(a: Activity, out: Bundle) { owner?.registry?.performSave(out) }
        })
    }
}

private class ComposeHostOwner : LifecycleOwner, ViewModelStoreOwner, SavedStateRegistryOwner {
    val registry = LifecycleRegistry(this)
    override val lifecycle get() = registry
    override val viewModelStore = ViewModelStore()
    private val controller = SavedStateRegistryController.create(this)
    override val savedStateRegistry get() = controller.savedStateRegistry
}
```

No `java.lang.reflect.Proxy`, no `InvocationHandler`, no JNI past step 6 above — matches the "favor
real Kotlin over reflection" recommendation from §2/§3 directly.

### 5.3 Open items this design surfaces but does not resolve (be explicit, don't paper over)

1. **Embedded-blob size for Compose specifically is unmeasured** (§2.4's corollary) — budget a real
   build experiment before assuming it's cheap the way `GameHub`'s Dialog-based blobs were.
2. **`onActivityCreated` firing for exactly this Activity instance is structurally guaranteed by JLS
   class-init ordering (§1.4)**, not by an Android-internals citation this report could pin to an
   exact source line — treat as "verify once on-device," matching this project's own established
   verify-empirically ethos (`ARCHITECTURE.md` §9/§13), rather than an unverified assumption.
3. **`src/platform/android/*` and `include/konative/platform/android/*` are stale** (built for
   `android_native_app_glue`/`NativeActivity`, per their own file contents) and need to be replaced
   or heavily rewritten around a `JNI_OnLoad` entry point — a direct, concrete implication of this
   research for the existing codebase, not just an abstract design note.

---

## Sources

**Official Android docs:** [Restrictions on non-SDK interfaces](https://developer.android.com/guide/app-compatibility/restrictions-non-sdk-interfaces) ·
[Non-SDK changes, Android 10](https://developer.android.com/about/versions/10/non-sdk-q) ·
[Non-SDK changes, Android 16](https://developer.android.com/about/versions/16/changes/non-sdk-16) ·
[NDK JNI tips (DefineClass unsupported)](https://developer.android.com/ndk/guides/jni-tips) ·
[Application.ActivityLifecycleCallbacks reference](https://developer.android.com/reference/android/app/Application.ActivityLifecycleCallbacks) ·
[NativeActivity reference](https://developer.android.com/reference/android/app/NativeActivity) ·
[androidx.lifecycle release notes](https://developer.android.com/jetpack/androidx/releases/lifecycle) ·
[Lifecycle in Jetpack Compose](https://developer.android.com/topic/libraries/architecture/lifecycle).

**Hidden-API flag data (downloaded and grepped directly):**
`https://dl.google.com/developers/android/qt/non-sdk/hiddenapi-flags.csv` ·
`https://dl.google.com/developers/android/baklava/non-sdk/hiddenapi-flags.csv`.

**Proxy/reflection/dex:** [android-jni-bridge Proxy.cpp](https://github.com/bitter/android-jni-bridge/blob/master/Proxy.cpp) ·
[Unity-Technologies fork](https://github.com/Unity-Technologies/android-jni-bridge/blob/master/Proxy.cpp) ·
[Aghajari — Proxies and Class Loading in Java and Android](https://medium.com/@aghajari/dynamic-code-execution-proxies-and-class-loading-in-java-and-android-97134d10be0c).

**Compose-without-Activity:** [helw.net — Compose UI without an Activity](https://helw.net/2025/08/31/compose-ui-without-an-activity/) ·
[JetBrains/compose-multiplatform#3203](https://github.com/JetBrains/compose-multiplatform/issues/3203) ·
[Compose in WindowManager overlays](https://medium.com/@rohitkasanwal/using-jetpack-compose-in-windowmanager-overlays-a-case-study-a8aea57cb44d) ·
[Compose inside a Service](https://www.techyourchance.com/jetpack-compose-inside-android-service/) ·
[Compose overlay Service gist](https://gist.github.com/handstandsam/6ecff2f39da72c0b38c07aa80bbb5a2f).

**Prior art (§4):** [utzcoz — Analyze NativeActivity](https://utzcoz.github.io/2021/12/25/Analyze-NativeActivity.html) ·
[MonoPackageManager.java](https://github.com/xamarin/xamarin-android/blob/main/src/java-runtime/java/mono/android/MonoPackageManager.java) ·
[FlutterActivity javadoc](https://api.flutter.dev/javadoc/io/flutter/embedding/android/FlutterActivity.html) ·
[Flutter engine boot walkthrough](https://xiongcen.github.io/2020/09/01/flutter-booting/) ·
[Frida native-function instrumentation](https://codeshare.frida.re/@salecharohit/instrumenting-native-android-functions-using-frida/).

**Local (this workspace):** [`research/research.md`](research.md) §1.1–1.2 ·
[`../../GameHub/libs/jni/src/dex_loader.cpp`](../../GameHub/libs/jni/src/dex_loader.cpp) ·
[`../../GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp`](../../GameHub/libs/jni/include/gamehub/jni/dex_loader.hpp) ·
[`../../GameHub/testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java`](../../GameHub/testapp/app/src/main/java/com/gamehub/testapp/MainActivity.java) ·
[`../../GameHub/cmake/modules/JvmDex.cmake`](../../GameHub/cmake/modules/JvmDex.cmake) ·
[`../testapp/app/src/main/java/com/konative/testapp/MainActivity.kt`](../testapp/app/src/main/java/com/konative/testapp/MainActivity.kt) ·
[`../testapp/README.md`](../testapp/README.md) · [`../ARCHITECTURE.md`](../ARCHITECTURE.md) ·
[`../src/platform/android/activity_bridge.cpp`](../src/platform/android/activity_bridge.cpp) ·
[`../include/konative/platform/android/README.md`](../include/konative/platform/android/README.md).
