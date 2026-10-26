#!/usr/bin/env bash
# Builds Xenon and runs its tests inside Ladybird's CI image (see Meta/ci/build-in-ladybird-image.sh).
#
# Env:
#   CCACHE_TOKEN     slab-ccache's bearer token; without it nothing is cached, and a cold build takes a couple of hours
#   CACHE_READ_ONLY  true to read the caches without writing them (pull requests)
#   JOBS             parallel compile jobs; defaults to what memory allows, as a big LibWeb unit can take 1.5 GB
#
# slab-ccache (slab-infra host/phase7-ccache.sh) is a plain HTTP store, so it holds both ccache's objects and vcpkg's
# prebuilt packages, under /xenon/.
set -euo pipefail

cd "$(dirname "$0")/../.."
git config --global --add safe.directory '*'

cache_url=${CACHE_URL:-http://10.255.42.1:8642/xenon}
read_only=${CACHE_READ_ONLY:-false}

if [[ -n ${CCACHE_TOKEN:-} ]]; then
  # Ladybird's build already compiles through ccache when it finds it; point that at slab-ccache only. The checkout is
  # always /xenon in the container, so the cache's keys stay put from run to run.
  export CCACHE_REMOTE_ONLY=true
  export CCACHE_REMOTE_STORAGE="$cache_url/ccache|bearer-token=$CCACHE_TOKEN|read-only=$read_only|operation-timeout=5000"
  export CCACHE_BASEDIR=/xenon

  # Ladybird's build appends these to its own local vcpkg cache.
  mode=$([[ $read_only == true ]] && echo read || echo readwrite)
  export VCPKG_BINARY_SOURCES="http,$cache_url/vcpkg/{name}/{version}/{sha}.zip,$mode,Authorization: Bearer $CCACHE_TOKEN"
  echo "caches: slab-ccache at $cache_url, read-only=$read_only"
else
  echo "no CCACHE_TOKEN: building without slab-ccache"
fi

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
exit $status
