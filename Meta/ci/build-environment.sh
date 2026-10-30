# shellcheck shell=bash

git config --global --add safe.directory '*'

mkdir -p Build/caches/ccache
export CCACHE_DIR=$PWD/Build/caches/ccache
export CCACHE_BASEDIR=$PWD
export CCACHE_MAXSIZE=${CCACHE_MAXSIZE:-3G}
if [[ -n ${CCACHE_TOKEN:-} ]]; then
  export CCACHE_REMOTE_STORAGE="http://10.255.42.1:8642/xenon|bearer-token=$CCACHE_TOKEN|operation-timeout=5000"
fi
unset CCACHE_TOKEN

if [[ -z ${JOBS:-} ]]; then
  memory_gb=$(($(awk '/MemTotal/ { print $2 }' /proc/meminfo) / 1024 / 1024))
  JOBS=$((memory_gb * 2 / 3))
  ((JOBS > $(nproc))) && JOBS=$(nproc)
  ((JOBS < 1)) && JOBS=1
fi
echo "jobs: $JOBS of $(nproc) CPUs"

export CC=clang-22 CXX=clang++-22
ccache --zero-stats >/dev/null 2>&1 || true
