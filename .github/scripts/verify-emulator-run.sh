#!/usr/bin/env bash
# Runs INSIDE android-build.yml's android-emulator-verify job, invoked as a real script file (not
# an inline YAML `script:` block) specifically because reactivecircus/android-emulator-runner does
# NOT run a multi-line `script:` value as one shell script the way an ordinary `run: |` step does -
# a real first CI run confirmed it splits on newlines and runs EACH LINE as its own independent
# `sh -c '<line>'` invocation ("Syntax error: end of file unexpected (expecting "fi")" - the if/fi
# below split across two separate processes, and any local shell variable set on one "line" would
# be invisible to the next for the same reason). Every check below needs real, persistent shell
# state across multiple statements, so it has to be a real script bash reads as a whole, not a
# YAML string this action re-splits.
#
# Installs the real APK, launches the real MainActivity, then checks real logcat output for either
# of the two ways a startup can genuinely fail - a crash, or a reported self-check failure - rather
# than just checking "did `am start` return 0" (which would NOT have caught this project's worst
# bug, the rotation crash, either: that bug left the process running with a blank screen, not
# crashed). Asserting "at least 8" rather than "exactly 8" self-check PASSED lines is deliberate:
# robust to a future 9th self-check being added without this threshold silently going stale, while
# still catching "on_started() never ran at all" (0 or near-0 matches) just as reliably as an exact
# count would.

set -uo pipefail

# Set by android-emulator-verify-koreload where Konative is checked out to a named subdirectory,
# not $GITHUB_WORKSPACE itself (a true sibling KoReload checkout needs that layout - see that job's
# own comment). Defaults to $GITHUB_WORKSPACE, preserving this script's original behavior for the
# plain android-emulator-verify job, which checks out Konative directly at the workspace root.
REPO_DIR="${KONATIVE_REPO_DIR:-$GITHUB_WORKSPACE}"

LOGCAT_PATH="$GITHUB_WORKSPACE/logcat.txt"
SCREENSHOT_PATH="$GITHUB_WORKSPACE/konative_emulator_screenshot.png"

APK_PATH="$(find "$REPO_DIR/testapp/app/build" -iname '*.apk' | head -n1)"
if [ -z "$APK_PATH" ]; then
  echo "::error::no .apk found under $REPO_DIR/testapp/app/build - the Gradle build step above should have produced one (see testapp/README.md's own 'Where the APK actually lands' note: the exact path is Gradle-version-dependent, this searches the whole tree rather than a fixed path)"
  exit 1
fi
echo "Installing $APK_PATH"
adb install -t -r "$APK_PATH"

# Set by android-emulator-verify-koreload only - inert for the plain job above. A fresh install has
# KoReload compiled in but nothing to load (this exact gap is what this job's own first CI run
# found - the app started cleanly, but neither module ever logged "status Ok", because nothing had
# ever pushed a generation-1 module file into config_directory_). Push both module .sos the same
# way koreload_cli's own push_to_app_storage() does for a real non-root device: stage under
# /data/local/tmp, then `run-as` copy into the app's own private storage - run-as works right after
# install, the app doesn't need to have been launched first (PackageManager creates the private
# data directory at install time). Matches koreload_module_path()'s own real naming convention
# (jni_onload.cpp) exactly - koreload_<name>.gen1.so.
if [ "${KORELOAD_EXPECTED:-0}" = "1" ]; then
  PKG="com.konative.testapp"
  FILES_DIR="/data/data/$PKG/files"
  for pair in "pointer_follow:$REPO_DIR/build/android-x86_64/src/koreload_modules/pointer_follow/konative_pointer_follow.so" \
              "waypoint_cycler:$REPO_DIR/build/android-x86_64/src/koreload_modules/waypoint_cycler/konative_waypoint_cycler.so"; do
    name="${pair%%:*}"
    local_so="${pair#*:}"
    if [ ! -s "$local_so" ]; then
      echo "::error::$local_so is missing or empty - the 'Build the two KoReload module .sos independently' step above should have produced it"
      exit 1
    fi
    staging="/data/local/tmp/koreload_${name}.gen1.so"
    dest="$FILES_DIR/koreload_${name}.gen1.so"
    echo "Pushing $local_so -> device:$dest"
    adb push "$local_so" "$staging"
    adb shell run-as "$PKG" cp "$staging" "$dest"
    adb shell run-as "$PKG" chmod 755 "$dest"
    adb shell rm -f "$staging"
  done
fi

adb logcat -c
adb shell am start -n com.konative.testapp/.MainActivity
sleep 15
adb logcat -d > "$LOGCAT_PATH"
adb exec-out screencap -p > "$SCREENSHOT_PATH" || true
echo "----- logcat tail -----"
tail -n 250 "$LOGCAT_PATH"

if grep -qE "FATAL EXCEPTION|Process: com\.konative\.testapp.*has died" "$LOGCAT_PATH"; then
  echo "::error::com.konative.testapp crashed on launch - see the uploaded logcat.txt artifact"
  exit 1
fi

if grep -q "self-check FAILED" "$LOGCAT_PATH"; then
  echo "::error::at least one real on-device self-check reported FAILED - see the uploaded logcat.txt artifact"
  exit 1
fi

PASS_COUNT=$(grep -c "self-check PASSED" "$LOGCAT_PATH" || true)
echo "Self-checks PASSED: $PASS_COUNT (expect at least 8 - jni_onload.cpp's on_started())"
if [ "$PASS_COUNT" -lt 8 ]; then
  echo "::error::expected at least 8 'self-check PASSED' lines in logcat, found $PASS_COUNT - on_started() likely never ran (native library failed to load, or install() never fired)"
  exit 1
fi

# Set by android-emulator-verify-koreload only (ARCHITECTURE.md section 12) - inert for the plain
# job above, which never sets this. Confirms both KoReload modules actually loaded, not just that
# the app itself started cleanly - matching jni_onload.cpp's own setup_koreload_modules() log text
# exactly (koreload::to_string(LoadStatus::Ok) == "Ok").
if [ "${KORELOAD_EXPECTED:-0}" = "1" ]; then
  if ! grep -q "KoReload pointer_follow module initial load -> status Ok" "$LOGCAT_PATH"; then
    echo "::error::KONATIVE_ENABLE_KORELOAD=ON build did not log a successful pointer_follow initial load - see the uploaded logcat.txt artifact"
    exit 1
  fi
  if ! grep -q "KoReload waypoint_cycler module initial load -> status Ok" "$LOGCAT_PATH"; then
    echo "::error::KONATIVE_ENABLE_KORELOAD=ON build did not log a successful waypoint_cycler initial load - see the uploaded logcat.txt artifact"
    exit 1
  fi
  echo "Both KoReload modules (pointer_follow, waypoint_cycler) reported a successful initial load."
fi
