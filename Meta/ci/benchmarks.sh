#!/usr/bin/env bash
# Builds what one benchmark suite needs and runs it through Meta/measure.py, as .github/workflows/benchmarks.yml does:
# on Linux inside Ladybird's CI image (Meta/ci/build-in-ladybird-image.sh), on macOS on the runner itself.
#
#   Meta/ci/benchmarks.sh web-benchmarks ITERATIONS [BENCHMARKS]   # Ladybird's suites, in headless Xenon
#   Meta/ci/benchmarks.sh js-benchmarks ITERATIONS [SUITES]        # Ladybird's suites, in the js and wasm REPLs
#   Meta/ci/benchmarks.sh bench                                    # BrowserBench, in headless Xenon
#
# The log goes to Build/benchmarks/SUITE.json, which `Meta/measure.py report` reads. BENCHMARKS_PYTHON is the Python to
# run Meta/measure.py with (and so the runners' venvs), for a runner whose own python3 is too new for their packages.
set -euo pipefail

cd "$(dirname "$0")/../.."
suite=$1
iterations=${2:-5}
selection=${3:-all}

if [[ $(uname) == Linux ]]; then
  # shellcheck source=/dev/null
  source Meta/ci/build-environment.sh
  xenon=Build/release/xenon
else
  JOBS=$(sysctl -n hw.ncpu)
  xenon=Build/release/bin/Xenon.app
fi
python=${BENCHMARKS_PYTHON:-python3}
measure=("$python" Meta/measure.py --log "Build/benchmarks/$suite.json")
mkdir -p Build/benchmarks

case $suite in
  js-benchmarks)
    # The REPLs need only LibJS and LibWasm, which Xenon's build has already compiled.
    ./Meta/xenon.py --jobs "$JOBS" build js
    ./Meta/xenon.py --jobs "$JOBS" build wasm
    "${measure[@]}" js-benchmarks --build-dir Build/release --iterations "$iterations" --suites "$selection"
    ;;
  web-benchmarks | bench)
    ./Meta/xenon.py --jobs "$JOBS" build
    arguments=(web-benchmarks --xenon "$xenon" --iterations "$iterations" --benchmarks "$selection")
    if [[ $suite == bench ]]; then
      arguments=(bench --xenon "$xenon" --dump-dir Build/benchmarks/bench-dumps)
    fi
    if [[ $(uname) == Linux ]]; then
      # Xenon runs headless, but as in its tests, with an X server and a D-Bus session of its own.
      # shellcheck source=/dev/null
      source Meta/ci/start-xvfb.sh
      dbus-run-session -- "${measure[@]}" "${arguments[@]}"
    else
      "${measure[@]}" "${arguments[@]}"
    fi
    ;;
  *)
    echo "error: unknown suite $suite (web-benchmarks, js-benchmarks, or bench)" >&2
    exit 1
    ;;
esac
