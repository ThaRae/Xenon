#!/usr/bin/env bash
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
