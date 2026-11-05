#!/usr/bin/env bash
set -euo pipefail

packages=(autoconf autoconf-archive automake bash ccache coreutils libtool nasm ninja pkg-config unzip)

if [ "$(uname -m)" = arm64 ]; then
    brew update --quiet
    brew unlink openssl@3 || true
    brew install --quiet "${packages[@]}" llvm@22
    exit 0
fi

export HOMEBREW_NO_AUTO_UPDATE=1
export HOMEBREW_NO_INSTALL_UPGRADE=1
export HOMEBREW_NO_INSTALLED_DEPENDENTS_CHECK=1
export HOMEBREW_NO_INSTALL_CLEANUP=1

brew unlink openssl@3 || true

missing=()
for package in "${packages[@]}"; do
    if installed=$(brew list --versions "$package" 2> /dev/null) && [ -n "$installed" ]; then
        echo "On the runner already: $installed"
    else
        missing+=("$package")
    fi
done

if [ "${#missing[@]}" -gt 0 ]; then
    echo "Installing: ${missing[*]}"
    brew install --quiet "${missing[@]}"
fi
