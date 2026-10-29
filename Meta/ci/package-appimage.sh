#!/usr/bin/env bash
# Builds Xenon and packages it as an AppImage with Meta/release.py package, inside Ladybird's CI image (see
# Meta/ci/build-in-ladybird-image.sh). Meta/ci/build-environment.sh sets up the build as for CI's own (JOBS and
# CCACHE_MAXSIZE pass through to it). The AppImage, its .zsync file and its AppDir end up in Build/distribution.
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

# The AppImage carries Qt's Wayland platform and its geolocation backends, which the image doesn't have: it only runs
# Xenon on X11. Installed before the build, so that everything is built against the same Qt.
apt-get update -q -o APT::Update::Error-Mode=any
apt-get install -y -q --no-install-recommends qt6-wayland libqt6positioning6-plugins

./Meta/xenon.py --jobs "$JOBS" build
ccache --show-stats || true

./Meta/release.py package --no-build
