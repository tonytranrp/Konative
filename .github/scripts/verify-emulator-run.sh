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

LOGCAT_PATH="$GITHUB_WORKSPACE/logcat.txt"
SCREENSHOT_PATH="$GITHUB_WORKSPACE/konative_emulator_screenshot.png"

APK_PATH="$(find "$GITHUB_WORKSPACE/testapp/app/build" -iname '*.apk' | head -n1)"
if [ -z "$APK_PATH" ]; then
  echo "::error::no .apk found under $GITHUB_WORKSPACE/testapp/app/build - the Gradle build step above should have produced one (see testapp/README.md's own 'Where the APK actually lands' note: the exact path is Gradle-version-dependent, this searches the whole tree rather than a fixed path)"
  exit 1
fi
echo "Installing $APK_PATH"
adb install -t -r "$APK_PATH"

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
