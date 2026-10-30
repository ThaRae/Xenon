#!/usr/bin/env bash
# Builds test262-runner from the engine with Xenon's patches and runs test262 with it (Meta/test262.py), inside
# Ladybird's CI image (Meta/ci/build-in-ladybird-image.sh), as .github/workflows/test262.yml does. Only the runner is
# built: LibJS and what it needs, which Xenon's build has already compiled.
set -euo pipefail

cd "$(dirname "$0")/../.."
# shellcheck source=/dev/null
source Meta/ci/build-environment.sh

./Meta/xenon.py --jobs "$JOBS" build test262-runner
./Meta/test262.py
