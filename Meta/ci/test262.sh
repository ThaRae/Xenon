#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

./Meta/xenon.py --jobs "$JOBS" build test262-runner
./Meta/test262.py
