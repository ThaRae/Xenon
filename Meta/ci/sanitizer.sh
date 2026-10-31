#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

xenon=(./Meta/xenon.py --preset Sanitizer --jobs "$JOBS")
logs=$PWD/Build/sanitizer/sanitizer-logs
rm -rf "$logs"
mkdir -p "$logs/xenon" "$logs/ladybird"

asan_options="strict_string_checks=1:check_initialization_order=1:strict_init_order=1:detect_stack_use_after_return=1"
asan_options+=":allocator_may_return_null=1"
ubsan_options="print_stacktrace=1:print_summary=1:halt_on_error=1"

ladybird_test_timeout_seconds=3600

status=0
"${xenon[@]}" build
"${xenon[@]}" build xenon-tests

QT_QPA_PLATFORM=offscreen \
  ASAN_OPTIONS="$asan_options:detect_leaks=0:log_path=$logs/xenon/sanitizer" \
  UBSAN_OPTIONS="$ubsan_options:log_path=$logs/xenon/sanitizer" \
  dbus-run-session -- "${xenon[@]}" test --no-build || status=$?

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
