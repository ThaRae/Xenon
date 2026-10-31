#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh
# shellcheck source=/dev/null
source Meta/ci/start-xvfb.sh

status=0
dbus-run-session -- ./Meta/xenon.py --jobs "$JOBS" test || status=$?

if command -v pulseaudio >/dev/null; then
  export PULSE_SERVER=unix:/tmp/pulse-native
  pulseaudio --system -D --exit-idle-time=1800 \
    --load="module-native-protocol-unix auth-anonymous=1 socket=/tmp/pulse-native" \
    --load="module-null-sink sink_name=virtual_sink" || true
fi
ladybird_status=0
./Meta/xenon.py --jobs "$JOBS" ladybird-test --timeout 3600 || ladybird_status=$?
((ladybird_status == 0)) || echo "::error::Ladybird's own tests failed against the patched engine (see the ctest output above)"
((status != 0)) || status=$ladybird_status

ccache --show-stats > Build/release/ccache-stats.txt 2>&1 || true
ccache --print-stats > Build/release/ccache-stats.tsv 2>/dev/null || true

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
