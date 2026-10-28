#!/usr/bin/env bash
# Builds Xenon and runs its tests inside Ladybird's CI image (see Meta/ci/build-in-ladybird-image.sh).
#
# Env:
#   JOBS            parallel compile jobs; defaults to what memory allows, as a big LibWeb unit can take 1.5 GB
#   CCACHE_MAXSIZE  how big ccache's directory may grow (default 3G, which leaves room in GitHub's 10 GB cache)
#
# ccache keeps its objects in Build/caches/ccache, and Ladybird's build keeps vcpkg's prebuilt packages in
# ladybird/Build/caches. Both are in the checkout, so CI carries them from one run to the next (.github/workflows/ci.yml).
# ccache's counters for the run go in Build/release/ccache-stats.txt, for people, and ccache-stats.tsv, which CI reads
# to decide whether a pull request saves its own caches.
set -euo pipefail

cd "$(dirname "$0")/../.."
git config --global --add safe.directory '*'

mkdir -p Build/caches/ccache
export CCACHE_DIR=$PWD/Build/caches/ccache
export CCACHE_BASEDIR=$PWD
export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-3G}

if [[ -z ${JOBS:-} ]]; then
  memory_gb=$(($(awk '/MemTotal/ { print $2 }' /proc/meminfo) / 1024 / 1024))
  JOBS=$((memory_gb * 2 / 3))
  ((JOBS > $(nproc))) && JOBS=$(nproc)
  ((JOBS < 1)) && JOBS=1
fi
echo "jobs: $JOBS of $(nproc) CPUs"

export CC=clang-22 CXX=clang++-22
ccache --zero-stats >/dev/null 2>&1 || true

# Xenon's tests open its windows too (the session restore check: a headless run never saves its session), so they get a
# real X server, as on a desktop, rather than Qt's offscreen platform. Xvfb picks a free display and reports it through
# -displayfd once it's ready, and the tests run in a D-Bus session of their own, as a desktop session has.
x_server_log=$(mktemp)
display_file=$(mktemp)
Xvfb -displayfd 3 -screen 0 1920x1080x24 -nolisten tcp 3>"$display_file" >"$x_server_log" 2>&1 &
x_server_pid=$!
trap 'kill "$x_server_pid" 2>/dev/null || true' EXIT
for _ in $(seq 1 300); do
  if [[ -s $display_file ]] || ! kill -0 "$x_server_pid" 2>/dev/null; then
    break
  fi
  sleep 0.1
done
if [[ ! -s $display_file ]]; then
  cat "$x_server_log" >&2
  echo "error: Xvfb didn't start" >&2
  exit 1
fi
DISPLAY=":$(tr -d '[:space:]' < "$display_file")"
export DISPLAY QT_QPA_PLATFORM=xcb
echo "display: $DISPLAY (Xvfb)"

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
