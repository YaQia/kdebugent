#!/usr/bin/env bash
# Case 1 运行器：kmod 构建 → initramfs 组装 → N 次运行 + oracle 求值
# 用法: run-case1.sh <已构建的内核树> <outdir> <rbase|verify> [N]
#   rbase  → oracle present（基线：bug 应出现，测 R_base / 阴性对照）
#   verify → oracle absent（补丁：bug 应消失）
# 内核镜像取 <树>/arch/x86/boot/bzImage
set -euo pipefail
TREE=$1; OUT=$2; MODE=$3; N=${4:-10}
HERE=$(cd "$(dirname "$0")" && pwd)
M0=$(cd "$HERE/../.." && pwd)
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$OUT"
KERNEL="$TREE/arch/x86/boot/bzImage"
[[ -r "$KERNEL" ]] || { echo "找不到 $KERNEL（树构建完成了吗？）" >&2; exit 1; }

case "$MODE" in
  rbase)  ORACLE_MODE=present ;;
  verify) ORACLE_MODE=absent ;;
  *) echo "用法: $0 <树> <outdir> <rbase|verify> [N]" >&2; exit 2 ;;
esac

# 1. 触发模块（临时目录构建，产物不进仓库）
mkdir -p "$STAGE/kmod"
cp "$HERE/proxy_migrate_repro.c" "$HERE/Makefile" "$STAGE/kmod/"
make -C "$TREE" M="$STAGE/kmod" > "$OUT/kmod-build.log" 2>&1
echo "kmod: $(stat -c%s "$STAGE/kmod/proxy_migrate_repro.ko") 字节"

# 2. initramfs：零依赖 init + trigger.ko
gcc -static -O2 -Wall -o "$STAGE/init" "$M0/init/init.c"
cp "$STAGE/kmod/proxy_migrate_repro.ko" "$STAGE/trigger.ko"
( cd "$STAGE" && find . -print0 | cpio --null -o -H newc --quiet ) > "$OUT/case1.cpio"
echo "initrd: $(wc -c < "$OUT/case1.cpio") 字节（kernel=$(grep -m1 'Linux version' "$OUT/kmod-build.log" 2>/dev/null || echo "$KERNEL")）"

# 3. N 次运行 + oracle 求值（同一谓词，两种 fail_when，见技术方案 7.4）
QEMU_SMP=2 QEMU_MEM=512M QEMU_TIMEOUT=120 \
QEMU_APPEND_EXTRA="kdbg.kmod=/trigger.ko loglevel=8" \
  "$M0/scripts/measure-rbase.sh" "$KERNEL" "$OUT/case1.cpio" "$OUT" \
  "WARNING:.*set_task_cpu" "$ORACLE_MODE" "$N"
