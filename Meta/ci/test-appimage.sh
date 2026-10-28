#!/usr/bin/env bash
# Runs Meta/release.py test against the AppImage in Build/distribution, in a container of a distribution's own image
# that has only what a desktop system has: the libraries the AppImage leaves to the system (glibc, libstdc++, OpenGL,
# the core of X11 and Wayland, fontconfig...), fonts, certificates, a D-Bus session, and FUSE, which the AppImage mounts
# itself with. Nothing of the build is there, so a library the AppImage misses fails the test here rather than on
# someone's machine.
#
#   Meta/ci/test-appimage.sh [image]    an Ubuntu, Debian, Fedora or Arch Linux image; ubuntu:26.04 by default
set -euo pipefail

if [[ ${1:-} != --in-container ]]; then
  root=$(cd "$(dirname "$0")/../.." && pwd)
  image=${1:-ubuntu:26.04}
  docker=(docker)
  docker info >/dev/null 2>&1 || docker=(sudo docker)
  echo "image: $image"
  # The device and the capability let the AppImage mount itself, as it does on a desktop.
  exec "${docker[@]}" run --rm \
    --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor=unconfined \
    --volume "$root:/xenon" --workdir /xenon \
    --env CI=true --env PYTHONDONTWRITEBYTECODE=1 --env HOST_OWNER="$(id -u):$(id -g)" \
    "$image" bash Meta/ci/test-appimage.sh --in-container
fi

# In the container, as root.
if command -v apt-get >/dev/null; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -q
  apt-get install -y -q --no-install-recommends \
    python3 xvfb dbus fuse3 ca-certificates fonts-dejavu-core \
    libgl1 libegl1 libopengl0 libgl1-mesa-dri libgbm1 libdrm2 libxcb1 libx11-6 libx11-xcb1 libwayland-client0 \
    libasound2t64 libfontconfig1 libfreetype6 libharfbuzz0b libexpat1 libusb-1.0-0 libfribidi0
elif command -v dnf >/dev/null; then
  dnf install -y -q \
    python3 xorg-x11-server-Xvfb dbus-daemon dbus-tools fuse3 ca-certificates dejavu-sans-fonts \
    mesa-libGL mesa-libEGL libglvnd-opengl mesa-dri-drivers mesa-libgbm libdrm libxcb libX11 libX11-xcb \
    libwayland-client alsa-lib fontconfig freetype harfbuzz expat libusb1 fribidi
elif command -v pacman >/dev/null; then
  pacman -Syu --noconfirm --needed \
    python xorg-server-xvfb dbus fuse3 ca-certificates ttf-dejavu \
    libglvnd mesa libdrm libxcb libx11 wayland alsa-lib fontconfig freetype2 harfbuzz expat libusb fribidi
else
  echo "error: no apt-get, dnf or pacman in this image" >&2
  exit 1
fi

# shellcheck source=/dev/null
source Meta/ci/start-xvfb.sh
# shellcheck source=/dev/null
source /etc/os-release
echo "system: $PRETTY_NAME, $(ldd --version | head -n 1)"

# Should the container not let the AppImage mount itself, it runs from a copy it extracts instead, which tests all the
# same but how it's mounted. --appimage-mount stays up for as long as the image is mounted.
appimage=Build/distribution/Xenon-$(tr -d '[:space:]' < VERSION.txt)-x86_64.AppImage
chmod +x "$appimage"
"$appimage" --appimage-mount </dev/null >/dev/null 2>&1 &
mount_pid=$!
sleep 5
if kill -0 "$mount_pid" 2>/dev/null; then
  kill "$mount_pid"
  wait "$mount_pid" 2>/dev/null || true
else
  echo "::warning::The AppImage couldn't mount itself on $PRETTY_NAME, so it runs extracted"
  export APPIMAGE_EXTRACT_AND_RUN=1
fi

status=0
dbus-run-session -- python3 Meta/release.py test || status=$?
chown -R "$HOST_OWNER" Build/distribution
exit $status
