#!/usr/bin/env bash
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

# shellcheck disable=SC2016
exec "${docker[@]}" run --rm \
  --volume "$root:/xenon" --workdir /xenon \
  --env JOBS --env CCACHE_MAXSIZE --env CCACHE_TOKEN --env CI=true --env PYTHONDONTWRITEBYTECODE=1 \
  --env HOST_OWNER="$(id -u):$(id -g)" \
  "$image" bash -c '"$@"; status=$?; chown -R "$HOST_OWNER" /xenon; exit $status' bash "$@"
