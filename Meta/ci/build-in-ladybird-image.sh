#!/usr/bin/env bash
# Runs a command in Ladybird's own Linux CI image, at the version the pinned ladybird submodule names, with the
# checkout mounted at /xenon. CI uses this so Xenon builds with exactly the compilers, Qt, and system libraries that
# Ladybird itself is tested with, and bumping the submodule bumps them too.
#
#   Meta/ci/build-in-ladybird-image.sh Meta/ci/build-and-test.sh
#
# Passes through CCACHE_TOKEN, CACHE_READ_ONLY, and JOBS for Meta/ci/build-and-test.sh.
set -euo pipefail

die() { printf 'error: %s\n' "$*" >&2; exit 1; }

root=$(cd "$(dirname "$0")/../.." && pwd)
version_file=$root/ladybird/Meta/Docker/ci/VERSION
[[ -f $version_file ]] || die "the ladybird submodule isn't checked out (git submodule update --init --depth 1)"
image=ghcr.io/ladybirdbrowser/ladybird-ci:$(tr -d '[:space:]' < "$version_file")

docker=(docker)
docker info >/dev/null 2>&1 || docker=(sudo docker)

echo "image: $image"
for attempt in 1 2 3; do
  "${docker[@]}" pull --quiet "$image" && break
  [[ $attempt == 3 ]] && die "could not pull $image"
  sleep $((attempt * 10))
done

# Host networking, so the build reaches slab-ccache (10.255.42.1:8642) from inside the container. The image's tools
# (rustup, cargo) want root, so the command runs as root and then hands everything it wrote in the checkout back to the
# runner's user, who has to be able to clean it up after the job.
# shellcheck disable=SC2016 # The command line is expanded by the container's shell, not this one.
exec "${docker[@]}" run --rm --network host \
  --volume "$root:/xenon" --workdir /xenon \
  --env CCACHE_TOKEN --env CACHE_READ_ONLY --env JOBS --env CI=true --env PYTHONDONTWRITEBYTECODE=1 \
  --env HOST_OWNER="$(id -u):$(id -g)" \
  "$image" bash -c '"$@"; status=$?; chown -R "$HOST_OWNER" /xenon; exit $status' bash "$@"
