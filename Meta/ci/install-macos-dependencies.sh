#!/usr/bin/env bash
set -euo pipefail

if [ "$(uname -m)" = arm64 ]; then
    brew update --quiet
    brew unlink openssl@3 || true
    brew install --quiet autoconf autoconf-archive automake bash ccache coreutils libtool llvm@22 nasm ninja \
        pkg-config unzip
    exit 0
fi

macports_version=2.12.7
macports_macos=15-Sequoia
macports_sha256=87b2e87ed7adc4aca947b42eb7ff991614cc0edfccb486237f2bbc75f28aae63
macports_ports=(autoconf autoconf-archive automake bash ccache coreutils libtool nasm pkgconfig unzip)
ninja_version=1.13.2
ninja_sha256=c99048673aa765960a99cf10c6ddb9f1fad506099ff0a0e137ad8960a88f321b

macos_major=$(sw_vers -productVersion | cut -d . -f 1)
if [ "$macos_major" != "${macports_macos%%-*}" ]; then
    echo "::error::The MacPorts installer is for macOS ${macports_macos%%-*}, and this runner has macOS $macos_major"
    exit 1
fi

brew unlink openssl@3 || true

installer="${RUNNER_TEMP:-$(mktemp -d)}/MacPorts-$macports_version-$macports_macos.pkg"
curl --fail --silent --show-error --location --retry 3 --output "$installer" \
    "https://github.com/macports/macports-base/releases/download/v$macports_version/MacPorts-$macports_version-$macports_macos.pkg"
echo "$macports_sha256  $installer" | shasum -a 256 --check --quiet
sudo installer -pkg "$installer" -target / > /dev/null

export PATH="/opt/local/bin:/opt/local/sbin:$PATH"
sudo port -q sync
sudo port -N -b install "${macports_ports[@]}"

ninja_archive="${RUNNER_TEMP:-$(mktemp -d)}/ninja-$ninja_version-mac.zip"
curl --fail --silent --show-error --location --retry 3 --output "$ninja_archive" \
    "https://github.com/ninja-build/ninja/releases/download/v$ninja_version/ninja-mac.zip"
echo "$ninja_sha256  $ninja_archive" | shasum -a 256 --check --quiet
sudo unzip -q -o "$ninja_archive" ninja -d /opt/local/bin
/opt/local/bin/ninja --version

if [ -n "${GITHUB_PATH:-}" ]; then
    printf '%s\n' /opt/local/sbin /opt/local/bin >> "$GITHUB_PATH"
fi
port -q installed requested
