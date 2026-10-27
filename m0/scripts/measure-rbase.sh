# M0 基线复现率测量：N 次运行 + checker 求值 → R_base（技术方案 2.3 阶段一）
# 用法: measure-rbase.sh <kernel> <initrd> <outdir> <ERE> <present|absent> [N]
# 产物: <outdir>/{runs/<i>/…, r_base.json}
set -euo pipefail
KERNEL=$1; INITRD=$2; OUT=$3; PAT=$4; MODE=$5; N=${6:-10}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
HITS=0
for i in $(seq 1 "$N"); do
    if "$HERE/run-qemu.sh" "$KERNEL" "$INITRD" "$OUT/runs/$i" && \
       "$HERE/checker-console.sh" "$OUT/runs/$i/console.log" "$PAT" "$MODE"; then
        HITS=$((HITS + 1)); echo "== run $i: 判定成立"
    else
        echo "== run $i: 未成立"
    fi
done
echo "{\"runs\": $N, \"hits\": $HITS, \"R_base\": $(awk "BEGIN{print $HITS/$N}")}" | tee "$OUT/r_base.json"
