#!/usr/bin/env bash
# Launch the interactive offline REINFORCE training controller.
# Double-clicked from Windows via 训练启动.bat, or run directly from a WSL shell.
set -euo pipefail

ROOT="/home/bigrat/workspace/ns-allinone-3.36.1/ns-3.36.1"
CONTROLLER="$ROOT/scratch/reproduction/latest-version/project/run_simulation/auto_train_control.py"
cd "$ROOT"

# Activate the ns3-ai virtualenv (torch, numpy, the reinrate .so binding).
# shellcheck disable=SC1091
source "$ROOT/ns3ai_env/bin/activate"

profile="${REPRODUCTION_NS3_PROFILE:-}"
controller_args=()
while (($#)); do
    case "$1" in
        --ns3Profile)
            if (($# < 2)); then
                echo "错误: --ns3Profile 需要 default 或 optimized。" >&2
                exit 2
            fi
            profile="$2"
            shift 2
            ;;
        --ns3Profile=*)
            profile="${1#*=}"
            shift
            ;;
        *)
            controller_args+=("$1")
            shift
            ;;
    esac
done

if [[ -z "$profile" ]]; then
    if [[ -t 0 ]]; then
        echo "请选择 ns-3 构建模式："
        echo "  1) default   （开发构建）"
        echo "  2) optimized （优化构建，通常更快）"
        read -r -p "请输入 1 或 2 [默认 1]: " profile_choice
        case "${profile_choice:-1}" in
            1|default) profile="default" ;;
            2|optimized) profile="optimized" ;;
            *) echo "错误: 请输入 1、2、default 或 optimized。" >&2; exit 2 ;;
        esac
        echo
    else
        profile="default"
    fi
fi

case "$profile" in
    default)
        executable="$ROOT/build/scratch/reproduction/ns3.36.1-two-node-ht-default"
        ;;
    optimized)
        executable="$ROOT/build-optimized/scratch/reproduction/ns3.36.1-two-node-ht-optimized"
        ;;
    *)
        echo "错误: ns-3 profile 必须是 default 或 optimized（当前: $profile）。" >&2
        exit 2
        ;;
esac
if [[ ! -x "$executable" ]]; then
    echo "错误: 找不到可执行的 $profile 构建：$executable" >&2
    exit 2
fi
export REPRODUCTION_NS3_PROFILE="$profile"

echo "======================================================================"
echo "  离线 Window-20 REINFORCE 交互式训练"
echo "  工作目录: $ROOT"
echo "  ns-3 profile: $REPRODUCTION_NS3_PROFILE"
echo "  BE_MaxAmpduSize: 65535 (A-MPDU enabled)"
echo "  Decision mode: per A-MPDU"
echo "======================================================================"
echo

# -B: don't write .pyc; -u: unbuffered so progress prints appear live.
python3 -B -u "$CONTROLLER" "${controller_args[@]}"

status=$?
echo
echo "脚本已结束 (exit $status)。按回车关闭窗口。"
read -r _ || true
exit $status
