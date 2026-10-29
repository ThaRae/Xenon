# shellcheck shell=bash
# Sourced by the scripts that build Xenon inside Ladybird's CI image (Meta/ci/build-in-ladybird-image.sh): ccache, the
# compilers, and how many jobs to build with. Run from the checkout's root.
#
# Env:
#   JOBS            parallel compile jobs; defaults to what memory allows, as a big LibWeb unit can take 1.5 GB
#   CCACHE_MAXSIZE  how big ccache's directory may grow (default 3G, which leaves room in GitHub's 10 GB cache)
#
# ccache keeps its objects in Build/caches/ccache, and Ladybird's build keeps vcpkg's prebuilt packages in
# ladybird/Build/caches. Both are in the checkout, so CI carries them from one run to the next (.github/workflows/ci.yml).

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
