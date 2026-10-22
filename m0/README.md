# M0 技术验证

目标（技术方案 §12）：不用 LLM 的手工管线，在 x86_64 上以纯脚本走通
"复现 → R_base 测量 → 打补丁 → 验证 → 回归"全链路。
验收：Case 1 与 Case 2 各产出完整运行产物与验证矩阵，
oracle 求值器对两类 bug 各验证一种 type（console_panic / ftrace_assert）。

## 环境勘察结论（2026-10-22，本机）

- QEMU x86_64 + KVM：就绪；gcc 静态链接：就绪；cpio/make/flex/bison：就绪
- 缺：ccache（可选）、busybox（Case 2 ftrace 阶段再补）、容器、virtme-ng（取舍评估顺延）
- 内核树：`/home/yaqia/linux`（7.3 开发树，工作区干净，含全部 case commit）
- 相关 config：`CONFIG_PREEMPT=y`、`CONFIG_SCHED_CACHE=y`、`HZ=1000`
- proxy execution 无 Kconfig 门槛：`DEFINE_STATIC_KEY_TRUE(__sched_proxy_exec)` 默认开，
  boot 参数 `sched_proxy_exec=` 可控
- `SCHED_FEAT(DELAY_DEQUEUE, true)` 默认开（Case 2 前提成立）

## 目录

- `scripts/`：`run-qemu.sh`（单次运行）· `oracle-console.sh`（console 求值器）· `measure-rbase.sh`（R_base 测量）
- `init/`：`init.c`——零依赖 initramfs init（挂载、解析 `kdbg.*` cmdline、finit_module 加载模块触发器、执行用户态触发器、关机）
- `smoke/`：阶段零冒烟（宿主内核，验证管线机制本身）
- `cases/case1-proxy-warn/`：Case 1 触发模块与事实记录

## 运行

```sh
./smoke/run-smoke.sh        # 阶段零：机制冒烟（宿主内核，零依赖）
```

## Case 1 已核实事实（读自 /home/yaqia/linux 的 fe3c73d7bc76，2026-10-22）

- WARN 签名：`WARNING: kernel/sched/core.c:3389 at set_task_cpu+0x1d3/0x280`
  → oracle pattern：`WARNING:.*set_task_cpu`
- 场景：mutex owner 于 CPU1；waiter 于 CPU0 `migrate_disable()` 后阻塞；
  proxy 把阻塞调度上下文移至 CPU1；owner 解锁 → `try_to_wake_up` → WARN
- 触发器形态：**内核模块**（`migrate_disable()` 是内核 API，用户态不可达；
  原始复现器即 kmod——提交正文 trace 中的 `[proxy_migrate_repro]`）
- `Fixes: b049b81bdff6`（可 bisect）；基线 = `fe3c73d7bc76^`

## Case 1 阶段一步骤

1. 基线构建：worktree `~/kdebugent-build/base` @ `fd73f4a66598`（= `fe3c73d7bc76^`），
   config 增量见 `config-delta.diff`
2. 基线 R_base：`./run-case1.sh ~/kdebugent-build/base out-rbase rbase 10`
3. 补丁构建（增量）：worktree 内 `git checkout fe3c73d7bc76 && make -j"$(nproc)"`
4. 补丁验证：`./run-case1.sh ~/kdebugent-build/base out-patched verify 3`
5. 阴性对照（基线再跑，应全部命中）：`./run-case1.sh ~/kdebugent-build/base out-negctl rbase 3`
6. 回归冒烟：tier0 雏形（boot + smoke trigger）

## 产物约定

每次运行落盘 `console.log` + `qemu.exit`；R_base 测量产出 `r_base.json`。
这些目录结构即未来编排层产物库（技术方案附录 B）的雏形。
