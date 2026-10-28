#!/usr/bin/env bash
# Builds Xenon and runs its tests inside Ladybird's CI image (see Meta/ci/build-in-ladybird-image.sh).
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
