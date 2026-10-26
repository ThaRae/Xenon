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

mkdir -p Build
touch Build/.ci-started

# A token pasted with a trailing newline, or the wrong one, otherwise only shows up as a 401 on every cache request.
# Ask slab-ccache first: a missing object (404) means the token is good.
if [[ -n ${CCACHE_TOKEN:-} ]]; then
  CCACHE_TOKEN=$(printf '%s' "$CCACHE_TOKEN" | tr -d '[:space:]')
  # slab-ccache's token is 64 hex digits (slab-infra host/phase7-ccache.sh). A secret saved with something around it,
  # such as quotes or terminal escapes from a copy, still holds the token, so use that.
  if [[ ! $CCACHE_TOKEN =~ ^[0-9a-f]{64}$ ]]; then
    candidates=$(printf '%s' "$CCACHE_TOKEN" | grep -oE '[0-9a-f]{64}' || true)
    if [[ -n $candidates && $(printf '%s\n' "$candidates" | wc -l) -eq 1 ]]; then
      echo "warning: CCACHE_TOKEN has ${#CCACHE_TOKEN} characters around a 64-digit token; using the token (fix the secret)"
      CCACHE_TOKEN=$candidates
    fi
  fi
  code=$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 -H "Authorization: Bearer $CCACHE_TOKEN" "$cache_url/.token-check" || true)
  if [[ $code == 401 ]]; then
    echo "warning: slab-ccache refused CCACHE_TOKEN (${#CCACHE_TOKEN} characters); building without the caches"
    unset CCACHE_TOKEN
  elif [[ $code != 404 && $code != 200 ]]; then
    echo "warning: slab-ccache didn't answer (HTTP ${code:-none}); building without the caches"
    unset CCACHE_TOKEN
  fi
fi

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
