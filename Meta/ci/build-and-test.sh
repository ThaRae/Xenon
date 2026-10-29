#!/usr/bin/env bash
# Builds Xenon and runs its tests inside Ladybird's CI image (see Meta/ci/build-in-ladybird-image.sh), then Ladybird's own
# tests against the patched engine (Meta/xenon.py ladybird-test), as Ladybird's CI runs them in the same image. Both run,
# whatever the other's result, and the script fails if either does.
#
# ccache's counters for the run go in Build/release/ccache-stats.txt, for people, and ccache-stats.tsv, which CI reads
# to decide whether a pull request saves its own caches. Meta/ci/build-environment.sh sets up the build (JOBS and
# CCACHE_MAXSIZE pass through to it), and Meta/ci/start-xvfb.sh the display the tests open windows on.
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh
# shellcheck source=/dev/null
source Meta/ci/start-xvfb.sh

# The tests run in a D-Bus session of their own, as a desktop session has.
status=0
dbus-run-session -- ./Meta/xenon.py --jobs "$JOBS" test || status=$?

# Ladybird's CI gives its tests a PulseAudio server with a sink that plays nothing.
if command -v pulseaudio >/dev/null; then
  export PULSE_SERVER=unix:/tmp/pulse-native
  pulseaudio --system -D --exit-idle-time=1800 \
    --load="module-native-protocol-unix auth-anonymous=1 socket=/tmp/pulse-native" \
    --load="module-null-sink sink_name=virtual_sink" || true
fi
ladybird_status=0
./Meta/xenon.py --jobs "$JOBS" ladybird-test || ladybird_status=$?
((ladybird_status == 0)) || echo "::error::Ladybird's own tests failed against the patched engine (see the ctest output above)"
((status != 0)) || status=$ladybird_status

ccache --show-stats > Build/release/ccache-stats.txt 2>&1 || true
ccache --print-stats > Build/release/ccache-stats.tsv 2>/dev/null || true

# vcpkg keeps a failed dependency's build output in its buildtrees, which the CI log otherwise never shows. Print the
# logs of the ports it says failed, and only those.
install_log=Build/release/vcpkg-manifest-install.log
if ((status != 0)) && [[ -f $install_log ]]; then
  while IFS= read -r port; do
    for log in Build/vcpkg-release/buildtrees/"$port"/*-out.log Build/vcpkg-release/buildtrees/"$port"/*-err.log; do
      [[ -f $log ]] || continue
      echo "::group::$log"
      tail -n 80 "$log"
      echo "::endgroup::"
    done
  done < <(sed -n 's/^error: building \([^:]*\):.*/\1/p' "$install_log" | sort -u)
fi
exit $status
