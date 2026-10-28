#!/usr/bin/env bash
# Builds Xenon and runs its tests inside Ladybird's CI image (see Meta/ci/build-in-ladybird-image.sh).
#
# Env:
#   JOBS            parallel compile jobs; defaults to what memory allows, as a big LibWeb unit can take 1.5 GB
#   CCACHE_MAXSIZE  how big ccache's directory may grow (default 3G, which leaves room in GitHub's 10 GB cache)
#
# ccache keeps its objects in Build/caches/ccache, and Ladybird's build keeps vcpkg's prebuilt packages in
# ladybird/Build/caches. Both are in the checkout, so CI carries them from one run to the next (.github/workflows/ci.yml).
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

status=0
./Meta/xenon.py --jobs "$JOBS" test || status=$?

ccache --show-stats > Build/release/ccache-stats.txt 2>&1 || true

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
