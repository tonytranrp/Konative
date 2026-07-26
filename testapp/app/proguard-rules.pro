# testapp/ owns exactly one Kotlin file (MainActivity.kt - System.loadLibrary() and nothing else,
# see testapp/README.md's Hard Rules) - no reflection, no JNI-name-sensitive Kotlin surface for R8
# to need project-specific rules for. The real Compose UI code R8 needs rules for is minified
# separately, entirely outside Gradle/AGP's own pass - see embedded_kotlin/r8-rules.pro and
# ARCHITECTURE.md section 6.6.
