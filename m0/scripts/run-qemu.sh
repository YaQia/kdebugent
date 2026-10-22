# M0 QEMU 单次运行：boot + console 捕获 + 超时 + 退出码落盘
# 用法: run-qemu.sh <kernel> <initrd> <outdir> [额外 qemu 参数...]
# 环境变量:
#   QEMU_SMP          vCPU 数（默认 1）
#   QEMU_TOPOLOGY     完整 -smp 拓扑（如 "4,sockets=2,cores=2"，设置后覆盖 QEMU_SMP）
#   QEMU_MEM          内存（默认 512M）
#   QEMU_TIMEOUT      超时秒数（默认 180）
#   QEMU_APPEND_EXTRA 追加到内核 cmdline 的参数
# 产物: <outdir>/{console.log, qemu.exit}
set -uo pipefail
KERNEL=$1; INITRD=$2; OUTDIR=$3; shift 3
mkdir -p "$OUTDIR"
SMP=${QEMU_SMP:-1}
MEM=${QEMU_MEM:-512M}
TMO=${QEMU_TIMEOUT:-180}
APPEND="console=ttyS0 panic=-1 nokaslr ${QEMU_APPEND_EXTRA:-}"
ACCEL=(-enable-kvm -cpu host)
if [[ ! -w /dev/kvm ]]; then ACCEL=(); echo "run-qemu: 无 KVM，退回 TCG" >&2; fi
set +e
timeout -k 10 "$TMO" qemu-system-x86_64 \
    ${ACCEL+"${ACCEL[@]}"} \
    -m "$MEM" -smp "${QEMU_TOPOLOGY:-$SMP}" \
    -kernel "$KERNEL" -initrd "$INITRD" \
    -append "$APPEND" \
    -no-reboot -display none -monitor none \
    -serial "file:$OUTDIR/console.log" \
    "$@"
RC=$?
set -e
echo "$RC" > "$OUTDIR/qemu.exit"
LINES=$(wc -l < "$OUTDIR/console.log" 2>/dev/null || echo 0)
echo "run-qemu: rc=$RC, console ${LINES} 行 -> $OUTDIR"
