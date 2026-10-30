#!/usr/bin/env bash
set -euo pipefail

if [[ ${1:-} != --in-container ]]; then
  root=$(cd "$(dirname "$0")/../.." && pwd)
  image=${1:-ubuntu:26.04}
  docker=(docker)
  docker info >/dev/null 2>&1 || docker=(sudo docker)
  echo "image: $image"
  exec "${docker[@]}" run --rm \
    --device /dev/fuse --cap-add SYS_ADMIN --security-opt apparmor=unconfined \
    --volume "$root:/xenon" --workdir /xenon \
    --env CI=true --env PYTHONDONTWRITEBYTECODE=1 --env HOST_OWNER="$(id -u):$(id -g)" \
    "$image" bash Meta/ci/test-appimage.sh --in-container
fi

if command -v apt-get >/dev/null; then
  export DEBIAN_FRONTEND=noninteractive
  apt-get update -q
  apt-get install -y -q --no-install-recommends \
    python3 xvfb dbus fuse3 ca-certificates fonts-dejavu-core \
    libgl1 libegl1 libopengl0 libgl1-mesa-dri libgbm1 libdrm2 libxcb1 libx11-6 libx11-xcb1 libwayland-client0 \
    libasound2t64 libfontconfig1 libfreetype6 libharfbuzz0b libexpat1 libusb-1.0-0 libfribidi0
elif command -v dnf >/dev/null; then
  dnf install -y -q \
    python3 xorg-x11-server-Xvfb dbus-daemon dbus-tools fuse3 ca-certificates dejavu-sans-fonts dejavu-sans-mono-fonts \
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

appimage=Build/distribution/Xenon-$(tr -d '[:space:]' < VERSION.txt)-x86_64.AppImage
chmod +x "$appimage"
mount_output=$(mktemp)
"$appimage" --appimage-mount </dev/null >"$mount_output" 2>&1 &
mount_pid=$!
mounted=false
for _ in $(seq 1 20); do
  mount_point=$(head -n 1 "$mount_output")
  if [[ -n $mount_point ]] && timeout 5 test -x "$mount_point/AppRun"; then
    mounted=true
    break
  fi
  sleep 0.5
done
kill "$mount_pid" 2>/dev/null || true
wait "$mount_pid" 2>/dev/null || true
if [[ $mounted == true ]]; then
  echo "The AppImage mounts itself, at $mount_point"
else
  cat "$mount_output"
  echo "::warning::The AppImage couldn't mount itself on $PRETTY_NAME, so it runs extracted"
  export APPIMAGE_EXTRACT_AND_RUN=1
fi

status=0
dbus-run-session -- python3 Meta/release.py test || status=$?
chown -R "$HOST_OWNER" Build/distribution
exit $status
