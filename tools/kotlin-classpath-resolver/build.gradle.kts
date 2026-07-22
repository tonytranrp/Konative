import java.security.MessageDigest
import java.util.zip.ZipFile

// See this folder's own README.md for the real, documented gap this project closes:
// "real Maven dependency resolution from CMake is still an open problem"
// (cmake/modules/KonativeEmbedKotlinDex.cmake's own top comment). This is NOT an app module and
// NEVER produces an APK/AAR of its own - its only job is letting Gradle do real Maven dependency
// resolution against the exact same AndroidX/Compose/coroutines closure embedded_kotlin/'s Compose
// UI code actually needs, then exporting the resolved artifacts (both plain classes-jars AND the
// original, unmodified .aar files) into flat directories the CMake-driven kotlinc+aapt2+r8 pipeline
// (KonativeCompileKotlinDex.cmake) consumes directly via KONATIVE_KOTLIN_CLASSPATH_DIR/
// KONATIVE_AAPT2_AAR_DIR - the exact two directories a human previously had to assemble by hand.
//
// com.android.library (not a plain java-library module) deliberately - AAR dependency resolution
// needs AGP's own artifact-transform registration (AAR -> classes.jar) to produce usable jars for
// kotlinc; this machine's own real, working testapp/ build already proves this exact AGP+SDK setup
// works, so this reuses the same proven mechanism rather than a novel, untested plain-Gradle path.
plugins {
    id("com.android.library") version "8.5.2"
}

android {
    // A real package name is required by AGP but never actually used - this module compiles
    // nothing of its own, it only resolves dependencies.
    namespace = "com.konative.toolresolver"
    compileSdk = 36
}

// Every version pinned here is the EXACT version already proven working (embedded_kotlin/README.md's
// own hard-won bug-fix history was all found and fixed against this precise set) - matched via
// real byte-for-byte comparison against C:/Users/Tonyt/konative-toolchain/kotlin-classpath's already-
// working jars, then cross-checked live against Google's own Maven metadata as still-current stable
// releases (2026-07-22 research pass). Deliberately NOT bumped to newer available versions
// (activity 1.13.0, kotlinx-coroutines-android 1.11.0 both exist) - the goal here is reproducing a
// proven-working closure, not upgrading it; an untested newer pairing could reintroduce some of the
// real R8/resource-loading/coroutines-dispatcher bugs that README's own history took real, on-device
// debugging to find and fix the first time.
dependencies {
    // Compose runtime
    implementation("androidx.compose.runtime:runtime-android:1.11.4")

    // Compose UI - declared explicitly even though several of these are also transitive
    // dependencies of ui-android, for a deterministic, self-documenting resolver output.
    implementation("androidx.compose.ui:ui-android:1.11.4")
    implementation("androidx.compose.ui:ui-graphics-android:1.11.4")
    implementation("androidx.compose.ui:ui-text-android:1.11.4")
    implementation("androidx.compose.ui:ui-unit-android:1.11.4")
    implementation("androidx.compose.ui:ui-util-android:1.11.4")
    implementation("androidx.compose.ui:ui-geometry-android:1.11.4")

    // Compose Foundation
    implementation("androidx.compose.foundation:foundation-android:1.11.4")
    implementation("androidx.compose.foundation:foundation-layout-android:1.11.4")

    // Activity - needed transitively by Compose UI's own back-press/window-insets view-tree hooks
    // (embedded_kotlin/README.md documents a reflection-reachable androidx.activity.R$id field).
    // activity-compose is DELIBERATELY OMITTED: KonativeEntryPoint.kt hand-rolls LifecycleOwner/
    // ViewModelStoreOwner/SavedStateRegistryOwner rather than extending ComponentActivity.
    implementation("androidx.activity:activity:1.7.0")
    implementation("androidx.activity:activity-ktx:1.7.0")

    // Lifecycle
    implementation("androidx.lifecycle:lifecycle-runtime-android:2.11.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-android:2.11.0")
    implementation("androidx.lifecycle:lifecycle-runtime-compose-android:2.11.0")

    // SavedState
    implementation("androidx.savedstate:savedstate-android:1.5.0")

    // Coroutines - the Main-dispatcher factory embedded_kotlin/README.md's own "Dispatchers.Main
    // blocker" writeup depends on.
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
}

// Where both export tasks below write their output - a fresh, plain directory tree, never the
// real machine-local KONATIVE_KOTLIN_CLASSPATH_DIR/KONATIVE_AAPT2_AAR_DIR paths directly (those stay
// pointed at the already-proven-working hand-assembled directories until this resolver itself is
// proven to produce an equivalent, working result - see this folder's own README).
val resolvedOutputDir = layout.buildDirectory.dir("../resolved-output")

// Exports every resolved runtime dependency as a plain .jar, matching KONATIVE_KOTLIN_CLASSPATH_DIR's
// expected shape. kotlin-stdlib is deliberately excluded - KonativeEmbedKotlinDex.cmake's own doc
// comment establishes it's sourced from the kotlinc distribution itself instead.
//
// Deliberately does NOT use AGP's own "android-classes-jar" artifact transform (the obvious,
// first-tried approach) - a real bug found empirically (2026-07-22): that transform's output is
// missing META-INF/*.kotlin_module (confirmed via a direct file-list diff against the
// already-working hand-assembled classpath's jars - 40 files short, that one the load-bearing one).
// That's the Kotlin multi-file-facade metadata the compiler needs to resolve a top-level
// function/property spread across multiple source files in the same module (e.g.
// Modifier.background(), Modifier.focusable() - virtually every Compose extension function is
// declared this way) - AGP's transform correctly omits it because ITS OWN internal
// javac/d8-based compilation pipeline never needs it, but a direct standalone kotlinc invocation
// absolutely does. Symptom when it's missing: dozens of "unresolved reference" errors on ordinary
// imports, with the actual target .class file (e.g. BackgroundKt.class) genuinely present and
// byte-identical to the working jar - a deeply misleading error shape that looks like a missing
// dependency, not a stripped-metadata one. Real fix: extract classes.jar directly from each
// resolved AAR's own raw zip contents (via the "aar" artifact type + zipTree()), never through
// AGP's transform - this preserves the AAR's complete, original classes.jar untouched.
val exportClasspathJars by tasks.registering {
    group = "konative"
    description = "Exports the resolved AndroidX/Compose classpath as flat .jar files, matching KONATIVE_KOTLIN_CLASSPATH_DIR's expected shape."

    val aarFiles = configurations.getByName("debugRuntimeClasspath").incoming.artifactView {
        attributes {
            attribute(org.gradle.api.artifacts.type.ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE, "aar")
        }
        isLenient = true
    }.files
    val plainJarFiles = configurations.getByName("debugRuntimeClasspath").incoming.artifactView {
        attributes {
            attribute(
                org.gradle.api.artifacts.type.ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE,
                org.gradle.api.artifacts.type.ArtifactTypeDefinition.JAR_TYPE,
            )
        }
        isLenient = true
    }.files

    inputs.files(aarFiles)
    inputs.files(plainJarFiles)
    val outDirProvider = resolvedOutputDir.map { it.dir("kotlin-classpath") }
    outputs.dir(outDirProvider)

    doLast {
        val outDir = outDirProvider.get().asFile
        outDir.deleteRecursively()
        outDir.mkdirs()

        // Content-hash dedup, not path/name dedup - a real bug found empirically (2026-07-22): the
        // "aar" artifact view can resolve the SAME underlying dependency (androidx.print:print:1.0.0
        // was the real case that surfaced this) as two separate File entries with DIFFERENT names
        // (one a real "print-1.0.0.aar", one a generically-named "classes.aar" - root cause not
        // fully chased down, plausibly an AGP-internal repackaging transform also matching the "aar"
        // attribute) but byte-IDENTICAL classes.jar content inside both. r8 correctly refuses to dex
        // two different input jars that both define the same class - deduping by content hash (not
        // just by output filename) is what actually fixes this, since fixing only the naming would
        // still leave two distinct files with the same classes on disk.
        val seenHashes = mutableSetOf<String>()
        aarFiles.forEach { aar ->
            val classesJar = zipTree(aar).matching { include("classes.jar") }.files.singleOrNull()
            // Real bug found empirically (2026-07-22): for an AAR with NO classes.jar entry at all
            // (a pure resource-only AAR - androidx.print:print:1.0.0 was the real case), Gradle's
            // zipTree().matching{}.files does NOT return an empty collection as expected - it
            // returns one PHANTOM file that is itself a valid but genuinely empty zip archive (a
            // bare 22-byte PK end-of-central-directory record, zero entries). Every resource-only
            // AAR produces this SAME phantom (content-identical, so the hash-dedup above correctly
            // collapsed them to one), which got written out as a real, junk 0-class "classes.jar" -
            // harmless on its own, but confusing and pure noise in the exported classpath. Checking
            // the real zip entry count (not just "is this file non-null") is what actually
            // distinguishes a genuine classes.jar from this phantom placeholder.
            val hasRealEntries = classesJar != null && ZipFile(classesJar).use { it.entries().hasMoreElements() }
            if (hasRealEntries) {
                val hash = MessageDigest.getInstance("SHA-256")
                    .digest(classesJar!!.readBytes())
                    .joinToString("") { "%02x".format(it) }
                if (seenHashes.add(hash)) {
                    classesJar.copyTo(File(outDir, "${aar.nameWithoutExtension}.jar"), overwrite = true)
                }
            }
        }

        plainJarFiles
            .filterNot { it.name.startsWith("kotlin-stdlib") }
            .forEach { jar ->
                val hash = MessageDigest.getInstance("SHA-256")
                    .digest(jar.readBytes())
                    .joinToString("") { "%02x".format(it) }
                if (seenHashes.add(hash)) {
                    jar.copyTo(File(outDir, jar.name), overwrite = true)
                }
            }

        // Final, unconditional cleanup pass: whichever of the two loops above is actually
        // responsible for it (not fully pinned down - the in-loop entry-count guard on the AAR path
        // should already prevent this, but empirically a genuinely-empty jar still made it through
        // at least once), delete anything that lands in the output directory as a real, valid zip
        // with zero entries. Harmless to kotlinc/r8 either way (an empty jar can't define or
        // conflict with any class), but real, unexplained noise has no place in a directory meant
        // to be a clean, auditable stand-in for a hand-assembled one.
        outDir.listFiles { f -> f.extension == "jar" }?.forEach { jar ->
            val isEmpty = ZipFile(jar).use { !it.entries().hasMoreElements() }
            if (isEmpty) {
                jar.delete()
            }
        }
    }
}

// Exports the original, UNMODIFIED .aar files (not their extracted classes.jar - those carry no
// res/ content at all) for every AAR-packaged dependency in the closure, matching
// KONATIVE_AAPT2_AAR_DIR's expected shape (KonativeCompileKotlinDex.cmake's Step 1.5 needs the real
// res/ content inside these to link real R classes via aapt2). Plain-jar dependencies
// (kotlinx-coroutines-android) have no AAR form at all and are correctly absent from this output.
val exportAars by tasks.registering(Sync::class) {
    group = "konative"
    description = "Exports the resolved AndroidX/Compose dependencies' original .aar files, matching KONATIVE_AAPT2_AAR_DIR's expected shape."
    from(
        configurations.getByName("debugRuntimeClasspath").incoming.artifactView {
            attributes {
                attribute(
                    org.gradle.api.artifacts.type.ArtifactTypeDefinition.ARTIFACT_TYPE_ATTRIBUTE,
                    "aar",
                )
            }
            isLenient = true
        }.files
    )
    into(resolvedOutputDir.map { it.dir("aapt2-aars") })
}

tasks.register("resolveKonativeClasspath") {
    group = "konative"
    description = "Resolves and exports both the flat classpath jars and the original AARs in one step."
    dependsOn(exportClasspathJars, exportAars)
}
