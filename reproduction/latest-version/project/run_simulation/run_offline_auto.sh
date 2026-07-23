#!/usr/bin/env bash
# Launch the interactive offline REINFORCE training controller.
# Double-clicked from Windows via 训练启动.bat, or run directly from a WSL shell.
set -euo pipefail

ROOT="/home/bigrat/workspace/ns-allinone-3.36.1/ns-3.36.1"
cd "$ROOT"

# Activate the ns3-ai virtualenv (torch, numpy, the reinrate .so binding).
# shellcheck disable=SC1091
source "$ROOT/ns3ai_env/bin/activate"

export REPRODUCTION_NS3_PROFILE="${REPRODUCTION_NS3_PROFILE:-default}"

echo "======================================================================"
echo "  离线 Window-20 REINFORCE 交互式训练"
echo "  工作目录: $ROOT"
echo "  ns-3 profile: $REPRODUCTION_NS3_PROFILE"
echo "======================================================================"
echo

# -B: don't write .pyc; -u: unbuffered so progress prints appear live.
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
python3 -B -u "$SCRIPT_DIR/auto_train_control.py" "$@"

status=$?
echo
echo "脚本已结束 (exit $status)。按回车关闭窗口。"
read -r _ || true
exit $status
