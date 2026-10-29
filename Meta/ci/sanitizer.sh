#!/usr/bin/env bash
# Builds Xenon with Ladybird's Sanitizer preset (AddressSanitizer and UndefinedBehaviorSanitizer) and runs its tests, then
# Ladybird's own against the patched engine (Meta/xenon.py ladybird-test), as Ladybird's Sanitizer job runs them, inside
# Ladybird's CI image (Meta/ci/build-in-ladybird-image.sh), as .github/workflows/sanitizer.yml does. Both run, whatever
# the other's result, and the script fails if either does.
#
# What the sanitizers find goes to files, as in Ladybird's CI, so that a report from a helper process isn't lost in a
# test's captured output: Build/sanitizer/sanitizer-logs/xenon for Xenon's tests, and ladybird beside it for Ladybird's.
# An AddressSanitizer error or undefined behavior in Xenon's fails the script, since an error in a helper process may not
# fail the check that started it. Ladybird's tests are judged by ctest, as Ladybird's CI judges them, and what they
# reported is printed. ccache's counters go in Build/sanitizer/ccache-stats.txt.
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

xenon=(./Meta/xenon.py --preset Sanitizer --jobs "$JOBS")
logs=$PWD/Build/sanitizer/sanitizer-logs
rm -rf "$logs"
mkdir -p "$logs/xenon" "$logs/ladybird"

# The options Ladybird's Sanitizer test preset gives its tests (ladybird/CMakePresets.json), which ctest applies only with
# --preset: stricter checks, and undefined behavior that stops the process. Both sanitizers log to the same files,
# sanitizer.<pid>, since whichever runtime starts last sets where both write.
asan_options="strict_string_checks=1:check_initialization_order=1:strict_init_order=1:detect_stack_use_after_return=1"
asan_options+=":allocator_may_return_null=1"
ubsan_options="print_stacktrace=1:print_summary=1:halt_on_error=1"

# LibWeb's web tests take 12 minutes of a Release build on GitHub's runner, which has a quarter of the cores Ladybird's
# Sanitizer job has, and the sanitizers slow them severalfold, past the 30 minutes Ladybird's CI gives one test.
ladybird_test_timeout_seconds=3600

status=0
# Built first, so that only the tests run with the options below.
"${xenon[@]}" build
"${xenon[@]}" build xenon-tests

# Qt on X11 under AddressSanitizer trips over a buffer overflow in libxkbcommon-x11, which keeps Ladybird's CI from
# testing its own Qt frontend with the sanitizers; so Xenon's windows go to Qt's offscreen platform instead of an X
# server. Qt and the system libraries it loads keep memory until exit, which LeakSanitizer would report against Xenon.
QT_QPA_PLATFORM=offscreen \
  ASAN_OPTIONS="$asan_options:detect_leaks=0:log_path=$logs/xenon/sanitizer" \
  UBSAN_OPTIONS="$ubsan_options:log_path=$logs/xenon/sanitizer" \
  dbus-run-session -- "${xenon[@]}" test --no-build || status=$?

# Ladybird's CI gives its tests a PulseAudio server with a sink that plays nothing, and no display. ladybird-test builds
# the tests first, so the code generators the build runs get these options too.
if command -v pulseaudio >/dev/null; then
  export PULSE_SERVER=unix:/tmp/pulse-native
  pulseaudio --system -D --exit-idle-time=1800 \
    --load="module-native-protocol-unix auth-anonymous=1 socket=/tmp/pulse-native" \
    --load="module-null-sink sink_name=virtual_sink" || true
fi
ladybird_status=0
ASAN_OPTIONS="$asan_options:log_path=$logs/ladybird/sanitizer" \
  UBSAN_OPTIONS="$ubsan_options:log_path=$logs/ladybird/sanitizer" \
  "${xenon[@]}" ladybird-test --timeout "$ladybird_test_timeout_seconds" || ladybird_status=$?
((ladybird_status == 0)) || echo "::error::Ladybird's own tests failed against the patched engine (see the ctest output above)"
((status != 0)) || status=$ladybird_status

ccache --show-stats >Build/sanitizer/ccache-stats.txt 2>&1 || true

shopt -s nullglob
for tests in xenon ladybird; do
  reports=("$logs/$tests"/*)
  if ((${#reports[@]} == 0)); then
    echo "The sanitizers reported nothing in $tests's tests."
    continue
  fi
  for report in "${reports[@]}"; do
    echo "::group::$tests/$(basename "$report")"
    cat "$report"
    echo "::endgroup::"
  done
  if [[ $tests == xenon ]] && grep -q -e "ERROR: AddressSanitizer" -e "runtime error:" "${reports[@]}"; then
    echo "::error::The sanitizers found errors in Xenon's tests; their reports are above, and in the sanitizer-logs artifact"
    status=1
  else
    echo "::warning::The sanitizers reported something in $tests's tests; see above, and the sanitizer-logs artifact"
  fi
done
exit $status
