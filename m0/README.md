# M0 技术验证

目标（技术方案 §12）：不用 LLM 的手工管线，在 x86_64 上以纯脚本走通
"复现 → R_base 测量 → 打补丁 → 验证 → 回归"全链路。
验收：Case 1 与 Case 2 各产出完整运行产物与验证矩阵，
checker 求值器对两类 bug 各验证一种 type（console_panic / ftrace_assert）。

## 环境勘察结论（2026-10-22，本机）

- QEMU x86_64 + KVM：就绪；gcc 静态链接：就绪；cpio/make/flex/bison：就绪
- 缺：ccache（可选）、busybox（Case 2 ftrace 阶段再补）、容器、virtme-ng（取舍评估顺延）
- 内核树：`/home/yaqia/linux`（7.3 开发树，工作区干净，含全部 case commit）
- 相关 config：`CONFIG_PREEMPT=y`、`CONFIG_SCHED_CACHE=y`、`HZ=1000`
- proxy execution 有 Kconfig 门槛：`CONFIG_SCHED_PROXY_EXEC`（`depends on !SCHED_CLASS_EXT && EXPERT`）。
  你们的 .config 开着 sched_ext，case 内核需关掉它再开 SCHED_PROXY_EXEC；
  开启后 static key 默认生效，boot 参数 `sched_proxy_exec=` 可关
- `SCHED_FEAT(DELAY_DEQUEUE, true)` 默认开（Case 2 前提成立）

## 目录

- `scripts/`：`run-qemu.sh`（单次运行）· `checker-console.sh`（console 求值器）· `measure-rbase.sh`（R_base 测量）
- `init/`：`init.c`——零依赖 initramfs init（挂载、解析 `kdbg.*` cmdline、finit_module 加载模块触发器、执行用户态触发器、关机）
- `smoke/`：阶段零冒烟（宿主内核，验证管线机制本身）
- `cases/case1-proxy-warn/`：Case 1 触发模块与事实记录

## 运行

```sh
./smoke/run-smoke.sh        # 阶段零：机制冒烟（宿主内核，零依赖）
```

## Case 1 已核实事实（读自 /home/yaqia/linux 的 fe3c73d7bc76，2026-10-22）

- WARN 签名：`WARNING: kernel/sched/core.c:3389 at set_task_cpu+0x1d3/0x280`
  → checker pattern：`WARNING:.*set_task_cpu`
- 场景：mutex owner 于 CPU1；waiter 于 CPU0 `migrate_disable()` 后阻塞；
  proxy 把阻塞调度上下文移至 CPU1；owner 解锁 → `try_to_wake_up` → WARN
- 触发器形态：**内核模块**（`migrate_disable()` 是内核 API，用户态不可达；
  原始复现器即 kmod——提交正文 trace 中的 `[proxy_migrate_repro]`）
- `Fixes: b049b81bdff6`（可 bisect）；基线 = `fe3c73d7bc76^`
- 首轮探针未命中的两个根因（已修复）：① `CONFIG_SCHED_PROXY_EXEC` 与
  `CONFIG_SCHED_CLASS_EXT` 编译期互斥，原 .config 开着 sched_ext，proxy exec 被编译出局；
  ② owner 曾用 msleep 持锁——`find_proxy_task` 只在 owner runnable（on_rq）时才代理迁移，
  改为自旋持锁

## Case 1 阶段一：✅ 已完成（2026-10-22，VERIFIED-DETERMINISTIC）

| 协议阶段                           | 运行 | 结果                                                                       |
| ---------------------------------- | ---- | -------------------------------------------------------------------------- |
| 基线 R_base（present）             | 10   | 10/10 命中 → R_base = 1.0                                                  |
| 补丁验证（absent，`fe3c73d7bc76`） | 3    | 3/3 判定成立：WARN 消失而代理迁移仍发生（诊断行 `waiter task_cpu=1` 佐证） |
| 阴性对照（present，基线镜像回跑）  | 3    | 3/3 命中，无环境漂移                                                       |
| 回归冒烟（tier0 雏形）             | 1    | 补丁内核 boot + 触发器正常                                                 |

复现步骤（运行产物在仓库根的 `out-*` 目录，已 gitignore）：

1. 基线构建：worktree `~/kdebugent-build/base` @ `fd73f4a66598`（= `fe3c73d7bc76^`），
   config 增量见 `config-delta.diff`（SCHED_PROXY_EXEC=y、关 sched_ext）
2. 基线 R_base：`./run-case1.sh ~/kdebugent-build/base out-rbase rbase 10`
3. 备份基线镜像：`cp ~/kdebugent-build/base/arch/x86/boot/bzImage ~/kdebugent-build/bzImage-baseline`
4. 补丁构建（增量）：worktree 内 `git checkout fe3c73d7bc76 && make -j"$(nproc)"`
5. 补丁验证：`./run-case1.sh ~/kdebugent-build/base out-patched verify 3`
6. 阴性对照：`QEMU_SMP=2 QEMU_MEM=512M QEMU_TIMEOUT=120 QEMU_APPEND_EXTRA="kdbg.kmod=/trigger.ko loglevel=8" m0/scripts/measure-rbase.sh ~/kdebugent-build/bzImage-baseline out-rbase/case1.cpio out-negctl "WARNING:.*set_task_cpu" present 3`
7. 回归冒烟：`./m0/smoke/run-smoke.sh ~/kdebugent-build/base/arch/x86/boot/bzImage`

## 产物约定

每次运行落盘 `console.log` + `qemu.exit`；R_base 测量产出 `r_base.json`。
这些目录结构即未来编排层产物库（技术方案附录 B）的雏形。
