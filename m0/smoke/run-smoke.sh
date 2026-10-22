#!/usr/bin/env bash
# M0 阶段零冒烟：宿主内核 + 零依赖 initramfs
# 验证: initramfs 组装 → QEMU boot → console 捕获 → init 解析参数并执行触发器
#       → oracle 求值 → 关机退出。不依赖任何 case，只验证管线机制本身。
# 用法: ./run-smoke.sh [内核镜像]（默认 /usr/lib/modules/$(uname -r)/vmlinuz）
set -euo pipefail
cd "$(dirname "$0")"
KERNEL=${1:-/usr/lib/modules/$(uname -r)/vmlinuz}
[[ -r "$KERNEL" ]] || { echo "找不到可读的内核镜像: $KERNEL" >&2; exit 1; }
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
gcc -static -O2 -Wall -o "$STAGE/init" ../init/init.c
gcc -static -O2 -Wall -o "$STAGE/trigger" trigger.c
( cd "$STAGE" && find . -print0 | cpio --null -o -H newc --quiet ) > smoke.cpio
echo "smoke: initrd $(wc -c < smoke.cpio) 字节（init $(stat -c%s "$STAGE/init") + trigger $(stat -c%s "$STAGE/trigger")）"
QEMU_SMP=1 QEMU_MEM=256M QEMU_TIMEOUT=120 QEMU_APPEND_EXTRA="kdbg.trig=/trigger" \
    ../scripts/run-qemu.sh "$KERNEL" "$PWD/smoke.cpio" out
echo "---- console 尾部 ----"
tail -5 out/console.log
echo "----------------------"
../scripts/oracle-console.sh out/console.log "kdbg: smoke-done rc=0" present
