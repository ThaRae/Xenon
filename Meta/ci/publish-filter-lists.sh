#!/usr/bin/env bash
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

public_key=$(sed -n 's/^static constexpr auto PUBLIC_KEY = "\(.*\)"sv;$/\1/p' "$xenon/UI/FilterListUpdates.cpp")
{
  printf '\x30\x2a\x30\x05\x06\x03\x2b\x65\x70\x03\x21\x00'
  printf '%s' "$public_key" | openssl base64 -d -A
} >"$scratch/public.der"
openssl pkeyutl -verify -rawin -pubin -keyform DER -inkey "$scratch/public.der" -in "$output/manifest.json" \
  -sigfile "$scratch/signature" >/dev/null
echo "Signed manifest.json with the key Xenon checks it with"
