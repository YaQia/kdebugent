# M0 console checker 求值器（console_panic 型）：确定性谓词，不解读语义
# 用法: checker-console.sh <console.log> <ERE 匹配模式> <present|absent>
# 语义（对应技术方案 7.4 的同一谓词两次求值）:
#   present → 日志中出现模式即"判定成立"（基线复现：bug 出现）
#   absent  → 日志中不出现模式即"判定成立"（补丁验证：bug 消失）
# 输出: 判定结果 + 证据切片（前 5 条命中行）
# 退出码: 0=判定成立, 1=判定不成立, 2=用法错误
set -euo pipefail
[[ $# -eq 3 ]] || { echo "usage: $0 <console.log> <ERE> <present|absent>" >&2; exit 2; }
LOG=$1; PAT=$2; MODE=$3
[[ -f "$LOG" ]] || { echo "checker: 日志不存在: $LOG" >&2; exit 2; }
if MATCHES=$(grep -nE -- "$PAT" "$LOG"); then P=yes; else P=no; fi
if { [[ $MODE == present && $P == yes ]] || [[ $MODE == absent && $P == no ]]; }; then
    FIRED=true
else
    FIRED=false
fi
echo "checker: mode=$MODE matched=$P fired=$FIRED"
if [[ -n "${MATCHES:-}" ]]; then
    echo "evidence:"
    echo "$MATCHES" | head -5 | sed 's/^/  /'
fi
[[ $FIRED == true ]]
