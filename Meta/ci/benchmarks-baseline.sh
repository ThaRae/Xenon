#!/usr/bin/env bash
# Downloads the results of main's last run of a benchmark suite (.github/workflows/benchmarks.yml), for this run's
# summary to compare with: the newest of main's runs that has the artifact, other than this one. Says which run it was
# as the step's `run` output, and nothing if there's none yet; that isn't an error.
#
#   Meta/ci/benchmarks-baseline.sh benchmarks-linux-web-benchmarks Build/benchmarks/baseline
#
# Needs gh with GH_TOKEN (and GH_REPO), and the actions: read permission.
set -uo pipefail

artifact=$1
directory=$2
mkdir -p "$directory"

runs=$(gh run list --workflow benchmarks.yml --branch main --limit 30 --json databaseId,status \
  --jq '.[] | select(.status == "completed") | .databaseId') || {
  echo "::warning::Couldn't list main's runs of benchmarks.yml, so there's nothing to compare with"
  exit 0
}
for run in $runs; do
  [[ $run == "${GITHUB_RUN_ID:-}" ]] && continue
  if gh run download "$run" --name "$artifact" --dir "$directory" >/dev/null 2>&1; then
    echo "main's last $artifact: run $run"
    [[ -n ${GITHUB_OUTPUT:-} ]] && echo "run=$run" >>"$GITHUB_OUTPUT"
    exit 0
  fi
done
echo "main has no earlier $artifact to compare with"
