#!/usr/bin/env bash
# Builds Xenon's filter lists into a checkout of xenon-filter-lists and signs their manifest: what that repository's
# daily workflow (Meta/ci/filter-lists-workflow.yml) publishes, and what Xenon downloads (UI/FilterListUpdates.h).
#
#   Meta/ci/publish-filter-lists.sh <xenon-filter-lists checkout> [update-filter-lists.py's options...]
#
# Env:
#   FILTER_LISTS_SIGNING_KEY  the Ed25519 private key, in PEM, whose public half is PUBLIC_KEY in UI/FilterListUpdates.cpp
#
# Needs what xenon-ublock's tools/build.py does (Python 3.11+, cargo, Node.js), xenon-ublock checked out beside Xenon,
# and OpenSSL 3 or later.
set -euo pipefail

output=$(cd "$1" && pwd)
xenon=$(cd "$(dirname "$0")/../.." && pwd)

if [[ -z ${FILTER_LISTS_SIGNING_KEY:-} ]]; then
  echo "FILTER_LISTS_SIGNING_KEY isn't set" >&2
  exit 1
fi

"$xenon/Meta/update-filter-lists.py" --output "$output" "${@:2}"

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
printf '%s\n' "$FILTER_LISTS_SIGNING_KEY" >"$scratch/key.pem"
openssl pkeyutl -sign -rawin -inkey "$scratch/key.pem" -in "$output/manifest.json" -out "$scratch/signature"
{
  openssl base64 -A -in "$scratch/signature"
  echo
} >"$output/manifest.json.sig"

# Check the signature with the key Xenon has, so that lists signed with any other key are never published.
public_key=$(sed -n 's/^static constexpr auto PUBLIC_KEY = "\(.*\)"sv;$/\1/p' "$xenon/UI/FilterListUpdates.cpp")
{
  # An Ed25519 SubjectPublicKeyInfo is this DER prefix, then the 32-byte key.
  printf '\x30\x2a\x30\x05\x06\x03\x2b\x65\x70\x03\x21\x00'
  printf '%s' "$public_key" | openssl base64 -d -A
} >"$scratch/public.der"
openssl pkeyutl -verify -rawin -pubin -keyform DER -inkey "$scratch/public.der" -in "$output/manifest.json" \
  -sigfile "$scratch/signature" >/dev/null
echo "Signed manifest.json with the key Xenon checks it with"
