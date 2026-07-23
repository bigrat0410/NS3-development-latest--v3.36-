#!/usr/bin/env bash
set -euo pipefail
ROOT="/home/bigrat/workspace/ns-allinone-3.36.1/ns-3.36.1"
exec /bin/bash "$ROOT/scratch/reproduction/latest-version/project/run_simulation/run_offline_auto.sh" "$@"
