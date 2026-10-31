#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} != --in-container ]]; then
  root=$(cd "$(dirname "$0")/../.." && pwd)
  docker=(docker)
  docker info >/dev/null 2>&1 || docker=(sudo docker)
  # shellcheck disable=SC2016
  exec "${docker[@]}" run --rm \
    --volume "$root:/xenon" --workdir /xenon \
    --env CI=true --env PYTHONDONTWRITEBYTECODE=1 --env HOST_OWNER="$(id -u):$(id -g)" \
    archlinux:latest bash -c 'bash Meta/ci/test-aur-package.sh --in-container "$@"; status=$?
      chown -R "$HOST_OWNER" Packaging/aur Build/aur Build/caches/aur Build/distribution 2>/dev/null; exit $status' bash "$@"
fi
shift

update=false
if [[ ${1:-} == --update ]]; then
  update=true
  shift
fi
package=${1:?usage: Meta/ci/test-aur-package.sh [--update] <package>}
[[ -f Packaging/aur/$package/PKGBUILD ]] || { echo "error: no Packaging/aur/$package/PKGBUILD" >&2; exit 1; }

pacman -Syu --noconfirm --needed base-devel git namcap python xorg-server-xvfb dbus ttf-dejavu
[[ $package == xenon ]] && pacman -S --noconfirm --needed ccache
useradd --create-home builder
echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder

cache=/xenon/Build/caches/aur
mkdir -p "$cache" Build/aur
chown -R builder: "$cache"
as_builder() {
  sudo --user builder --set-home CCACHE_DIR="$cache/ccache" CCACHE_MAXSIZE="${CCACHE_MAXSIZE:-2G}" \
    LADYBIRD_MAIN_SOURCE_DIR="$cache/ladybird" -- "$@"
}

if [[ $update == true ]]; then
  chown -R builder: Packaging/aur
  as_builder python3 Meta/aur.py update
fi
as_builder python3 Meta/aur.py check "$package"

build_dir=/home/builder/$package
cp -r "Packaging/aur/$package" "$build_dir"
chown -R builder: "$build_dir"
(cd "$build_dir" && as_builder makepkg --syncdeps --noconfirm)
built=$(cd "$build_dir" && as_builder makepkg --packagelist)
cp "$built" Build/aur/

status=0
echo "==> namcap"
lint=$(namcap "$build_dir/PKGBUILD" "$built")
echo "$lint"
if grep -q ' E: ' <<<"$lint"; then
  echo "::error::namcap found errors in $package"
  status=1
fi

pacman -U --noconfirm "$built"
# shellcheck source=/dev/null
source Meta/ci/start-xvfb.sh
dbus-run-session -- python3 Meta/release.py test --installed /usr/bin/xenon || status=$?
exit $status
