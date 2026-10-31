#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

apt-get update -q -o APT::Update::Error-Mode=any
apt-get install -y -q --no-install-recommends qt6-wayland libqt6positioning6-plugins

./Meta/xenon.py --jobs "$JOBS" build
ccache --show-stats || true

./Meta/release.py package --no-build
