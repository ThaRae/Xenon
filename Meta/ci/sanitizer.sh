#!/usr/bin/env bash
# Builds Xenon with Ladybird's Sanitizer preset (AddressSanitizer and UndefinedBehaviorSanitizer) and runs its tests,
# inside Ladybird's CI image (Meta/ci/build-in-ladybird-image.sh), as .github/workflows/sanitizer.yml does. Ladybird's
# own CI builds and tests every change this way. Once Meta/xenon.py has ladybird-test, Ladybird's own tests run too, as
# in Ladybird's Sanitizer job.
#
# What the sanitizers find goes to files in Build/sanitizer/sanitizer-logs, as in Ladybird's CI, so that a report from a
# helper process isn't lost in a test's captured output. The script fails if any is an AddressSanitizer error or
# undefined behavior. ccache's counters go in Build/sanitizer/ccache-stats.txt.
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

xenon=(./Meta/xenon.py --preset Sanitizer --jobs "$JOBS")
logs=$PWD/Build/sanitizer/sanitizer-logs
rm -rf "$logs"
mkdir -p "$logs"

status=0
# Built first, so that only the tests run with the options below.
"${xenon[@]}" build
"${xenon[@]}" build xenon-tests

# Qt on X11 under AddressSanitizer trips over a buffer overflow in libxkbcommon-x11, which keeps Ladybird's CI from
# testing its own Qt frontend with the sanitizers; so Xenon's windows go to Qt's offscreen platform instead of an X
# server. Qt and the system libraries it loads keep memory until exit, which LeakSanitizer would report against Xenon.
QT_QPA_PLATFORM=offscreen \
  ASAN_OPTIONS="log_path=$logs/asan:detect_leaks=0" \
  UBSAN_OPTIONS="log_path=$logs/ubsan:print_stacktrace=1" \
  dbus-run-session -- "${xenon[@]}" test --no-build || status=$?

# Ladybird's own tests, with the options Ladybird's CI gives them.
if "${xenon[@]}" ladybird-test --help >/dev/null 2>&1; then
  if command -v pulseaudio >/dev/null; then
    export PULSE_SERVER=unix:/tmp/pulse-native
    pulseaudio --system -D --exit-idle-time=1800 \
      --load="module-native-protocol-unix auth-anonymous=1 socket=/tmp/pulse-native" \
      --load="module-null-sink sink_name=virtual_sink" || true
  fi
  ladybird_status=0
  ASAN_OPTIONS="log_path=$logs/asan" UBSAN_OPTIONS="log_path=$logs/ubsan" \
    "${xenon[@]}" ladybird-test || ladybird_status=$?
  ((ladybird_status == 0)) || echo "::error::Ladybird's own tests failed against the patched engine (see the ctest output above)"
  ((status != 0)) || status=$ladybird_status
else
  echo "Meta/xenon.py has no ladybird-test yet, so only Xenon's tests ran."
fi

ccache --show-stats >Build/sanitizer/ccache-stats.txt 2>&1 || true

shopt -s nullglob
reports=("$logs"/*)
if ((${#reports[@]} > 0)); then
  errors=$(grep -l -e "ERROR: AddressSanitizer" -e "runtime error:" "${reports[@]}" || true)
  for report in "${reports[@]}"; do
    echo "::group::$(basename "$report")"
    cat "$report"
    echo "::endgroup::"
  done
  if [[ -n $errors ]]; then
    echo "::error::The sanitizers found errors; their reports are above, and in the sanitizer-logs artifact"
    status=1
  fi
else
  echo "The sanitizers reported nothing."
fi
exit $status
