#!/usr/bin/env bash
# Case 1 运行器：kmod 构建 → initramfs 组装 → N 次运行 + checker 求值
# 用法: run-case1.sh <已构建的内核树> <outdir> <rbase|verify> [N]
#   rbase  → checker present（基线：bug 应出现，测 R_base / 阴性对照）
#   verify → checker absent（补丁：bug 应消失）
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
  rbase)  CHECK_MODE=present ;;
  verify) CHECK_MODE=absent ;;
  *) echo "用法: $0 <树> <outdir> <rbase|verify> [N]" >&2; exit 2 ;;
esac

# 1. 触发模块（临时目录构建，产物不进仓库）
#    KBUILD_MODPOST_WARN=1：精简构建（bzImage+vmlinux+modules_prepare）不产出
#    Module.symvers，modpost 无法在构建期校验符号。本树 MODVERSIONS 未开、
#    触发器只用 EXPORT_SYMBOL_GPL 符号、且与目标内核同树构建——符号校验
#    交给运行期的模块加载器。全量模块构建落地后可移除此开关。
mkdir -p "$STAGE/kmod"
cp "$HERE/proxy_migrate_repro.c" "$HERE/Makefile" "$STAGE/kmod/"
make -C "$TREE" M="$STAGE/kmod" KBUILD_MODPOST_WARN=1 > "$OUT/kmod-build.log" 2>&1 \
    || { tail -20 "$OUT/kmod-build.log" >&2; exit 1; }
echo "kmod: $(stat -c%s "$STAGE/kmod/proxy_migrate_repro.ko") 字节"

# 2. initramfs：零依赖 init + trigger.ko
gcc -static -O2 -Wall -o "$STAGE/init" "$M0/init/init.c"
cp "$STAGE/kmod/proxy_migrate_repro.ko" "$STAGE/trigger.ko"
( cd "$STAGE" && find . -print0 | cpio --null -o -H newc --quiet ) > "$OUT/case1.cpio"
echo "initrd: $(wc -c < "$OUT/case1.cpio") 字节（kernel=$(grep -m1 'Linux version' "$OUT/kmod-build.log" 2>/dev/null || echo "$KERNEL")）"

# 3. N 次运行 + checker 求值（同一谓词，两种 fail_when，见技术方案 5.4）
QEMU_SMP=2 QEMU_MEM=512M QEMU_TIMEOUT=120 \
QEMU_APPEND_EXTRA="kdbg.kmod=/trigger.ko loglevel=8" \
  "$M0/scripts/measure-rbase.sh" "$KERNEL" "$OUT/case1.cpio" "$OUT" \
  "WARNING:.*set_task_cpu" "$CHECK_MODE" "$N"
