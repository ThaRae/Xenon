#!/usr/bin/env bash
# Builds one of the AUR packages in Packaging/aur with makepkg, in a container of Arch Linux's own image, as an AUR user
# would: it checks that the .SRCINFO matches the PKGBUILD, builds the package, checks it with namcap, installs it with
# pacman, and runs Meta/release.py test's checks against the Xenon it installed. The package is left in Build/aur.
#
#   Meta/ci/test-aur-package.sh [--update] <package>
#
# xenon-bin installs the published AppImage, in minutes. xenon builds Xenon from source, which takes hours from a cold
# start; Build/caches/aur keeps ccache's objects and vcpkg's packages from one run to the next. With --update, it first
# bumps the packages to VERSION.txt's release (Meta/aur.py update), as the release workflow does once it's published.
set -euo pipefail

if [[ ${1:-} != --in-container ]]; then
  root=$(cd "$(dirname "$0")/../.." && pwd)
  docker=(docker)
  docker info >/dev/null 2>&1 || docker=(sudo docker)
  # The script runs as root, and then hands what it wrote in the checkout back to the runner's user, whatever happened.
  # shellcheck disable=SC2016 # The command line is expanded by the container's shell, not this one.
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

# In the container, as root. makepkg runs as a user of its own, who may install the build's dependencies with sudo. The
# checks need what a desktop has: an X server, a D-Bus session, fonts. Ladybird's build uses ccache when it's there.
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

# A copy to build in, so that what makepkg leaves stays out of the checkout.
build_dir=/home/builder/$package
cp -r "Packaging/aur/$package" "$build_dir"
chown -R builder: "$build_dir"
(cd "$build_dir" && as_builder makepkg --syncdeps --noconfirm)
built=$(cd "$build_dir" && as_builder makepkg --packagelist)
cp "$built" Build/aur/

# namcap's errors fail the test, and its warnings are only shown.
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
