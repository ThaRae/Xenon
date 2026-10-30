#!/usr/bin/env bash
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
