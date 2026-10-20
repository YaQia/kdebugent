# kdebugent 基准 Case 卡片

本文件是基准集的起点。每张卡片对应一个真实的历史 bug，以真实的修复 commit 为标准答案，包含五个部分：

- **元信息**：提交、作者、Fixes、真实性来源（CVE / LKML 报告 / 纯代码审查发现）；
- **机制**：bug 因果链，一段话说清；
- **触发构造**：复现程序的编排思路；
- **oracle**：判定规格草图（schema 见技术方案 7.2）；
- **环境与验证参数**：config、拓扑、R_base 预期、协议参数、风险与备选。

所有 case 的基线内核一律取修复 commit 的 parent（`git checkout <fix>^`）构建，验证时应用修复 commit 本身。首批三例：前两例用于 M0 管线验证，第三例是 M1 分析型旗舰。

---

## Case 1：proxy donors 的假迁移告警（崩溃类，M0）

| 项     | 内容                                                                                                |
| ------ | --------------------------------------------------------------------------------------------------- |
| commit | `fe3c73d7bc76` sched/core: Avoid false migration warning for proxy donors                           |
| 作者   | Andrea Righi（"Make proxy execution compatible with sched_ext" v9 系列 04/18），Peter Zijlstra 提交 |
| 真实性 | CVE-2026-98418；被 AUTOSEL 回灌 6.18 stable——WARN 在现实中确实触发过                                |
| 类别   | 崩溃类（WARN）→ `console_panic` 型 oracle                                                           |

**机制**：proxy execution 下，持锁任务（donor）替代阻塞的高优任务（proxy）运行。donor 被迁移时（如 `sched_setaffinity`）走到迁移路径的合法性检查，该检查未考虑 donor 身份，触发假告警。

**触发构造**（纯 C）：

```text
CPU0: 低优任务 H 持有 PI futex 并运行
CPU1: SCHED_FIFO 高优任务 W 阻塞在该锁上 → W 成为 proxy，H 成为 donor
主线程: sched_setaffinity(H, CPU1) → donor 迁移路径 → WARN
```

**oracle 草图**：

```json
{
  "trigger": {
    "type": "c",
    "build": "static-musl",
    "run": { "iterations": 100 }
  },
  "oracle": {
    "type": "console_panic",
    "spec": { "pattern": "WARNING:.*（签名待核对，见风险）" },
    "fail_when": "present"
  },
  "env": {
    "arch": "x86_64",
    "smp": 2,
    "memory_mb": 2048,
    "extra_config": "CONFIG_PROXY_EXECUTION=y"
  }
}
```

**验证参数**：编排正确则 WARN 确定性出现 → 预期 R_base ≈ 1.0，M=3；若时序敏感退化为统计模式（R_base ≥ 0.3 时 M 按表取）。

**风险**：① WARN 的具体签名与触发点需先读 `fe3c73d7bc76` 全文在本地树核对（M0 首个工作项）；② proxy/donor 关系的编排依赖 PI futex 时序，预计要迭代几轮；③ `CONFIG_PROXY_EXECUTION` 的依赖（PREEMPT 等）需核对。

**备选**：`f5741d2b3451`（wq_worker_tick 执行上下文，已进 7.2.x stable；缺点是触发依赖 tick 恰好落在 proxy 窗口内，确定性差一档）。

---

## Case 2：LLC 错误调度（行为类，M0）

| 项     | 内容                                                                                              |
| ------ | ------------------------------------------------------------------------------------------------- |
| commit | `0d6526f82c3c` sched/cache: Keep nr_pref_llc_running in the runnable domain                       |
| 作者   | Tim Chen；Reported-by: Zhan Xusheng（小米）；Suggested-by: Chen Yu                                |
| Fixes  | `714059f79ff0`（可 bisect 的真回归）；Cc stable v7.2.x，进 7.3-rc5                                |
| 报告   | lore.kernel.org/lkml/20260827135000.735138-1-zhanxusheng@xiaomi.com/（含现场负载描述，M0 前必读） |
| 类别   | 行为类 → `ftrace_assert` 型 oracle                                                                |

**机制**：`nr_pref_llc_running` 按 queued 语义计数，`h_nr_runnable` 按 runnable 语义计数。DELAY_DEQUEUE 让睡眠任务滞留在前者中，等式 `nr_pref_llc_running == cfs.h_nr_runnable` 失配 → `alb_break_llc()` 误判 → 主动负载均衡把任务拉离其偏好 LLC。

**触发构造**（纯 C）：

```text
QEMU: -smp 4,sockets=2,cores=2（两个 LLC）
CPU0-1（LLC0）: 共享 mm 的线程组，全体偏好 LLC0 的负载
其中一线程周期性睡眠 → 触发 delay-dequeue，计数滞留
源 CPU 保持繁忙直到 active load balance 启动
观测: 标记任务被迁往 LLC1
```

**oracle 草图**：

```json
{
  "oracle": {
    "type": "ftrace_assert",
    "spec": {
      "event": "sched:sched_migrate_task",
      "filter": "comm == 'llc-mark'",
      "condition": "dest_cpu 不在源任务的偏好 LLC 内",
      "window_s": 120,
      "min_count": 1
    },
    "fail_when": "present"
  },
  "env": {
    "arch": "x86_64",
    "smp": 4,
    "topology": "2 sockets × 2 cores",
    "extra_config": "CONFIG_SCHED_CACHE=y"
  }
}
```

注：LLC 成员表由 ArchProfile 按拓扑提供（guest 内读
`/sys/devices/system/cpu/cpu*/cache/index3/shared_cpu_list`）。

**验证参数**：active balance 触发依赖负载形状，预期 R_base 在 0.3–0.7 区间，N=10 测出实际值后按表定 M。

**风险**：① 负载形状（源 CPU 忙碌度、balance 间隔）需要调参，可能要几轮迭代；② `SCHED_DELAY_DEQUEUE` 的默认值需在树内 `kernel/sched/features.h` 核对。

**备选**：`f8610c57f407`（h_curr in throttle_cfs_rq，Wanwu Li）——cgroup `cpu.max` + 忙循环是全清单里最简单的触发，oracle 读 `cpu.stat` 超配额（`metric_threshold` 型）；若本 case 负载形状调不出来则换它。

---

## Case 3：place_entity 溢出（行为类·分析型旗舰，M1）

| 项     | 内容                                                                                                                      |
| ------ | ------------------------------------------------------------------------------------------------------------------------- |
| commit | `2f93bcb3f9aa` sched/fair: Avoid overflow in place_entity()                                                               |
| 作者   | Hui Su；Peter Zijlstra 提交（2026-10-16）                                                                                 |
| Fixes  | `4823725d9d1d`（可 bisect）                                                                                               |
| 真实性 | **无 Reported-by——纯代码审查发现，现场从未被捕获**。这正是它的价值所在                                                    |
| 类别   | 现场症状：**行为类**（无崩溃签名）；发现路径：**纯分析型**（vmcore 无事件证据）；验证 oracle：崩溃类形式（UBSAN console） |

**机制**：place_entity() 的 vlag 膨胀 `lag * (load + weight) / load` 中间乘积可先于商溢出 s64（4823725d9d1d 移除了 scale_load_down() 的算术余量）。修复为代数重写 `lag + lag * weight / load`，两行，教科书级靶向补丁。

**为什么现场抓不到——2.44 小时不变量**：

```
溢出条件  lag × (load + weight) > S64_MAX
lag 累积速率 = avg_vruntime 推进速率 = NICE_0_LOAD / sum_weight（恒定，与谁在跑无关）
溢出阈值   lag_needed = S64_MAX / W
所需时间   t = (S64_MAX / W) / (NICE_0 / W) = S64_MAX / NICE_0_LOAD ≈ 8.8e12 ns ≈ 2.44 h
```

与负载规模无关（加实体同时抬高阈值、等比降低累积速率）：**任何配置下都需要满负载浸润约两个半小时**。满载下两半小时才发作一次的算术 bug，现场抓不到是物理必然。

**无状态损坏——vmcore 抓不到有效信息**：溢出是事件，转储是状态。被污染的 vlag 随后会被 `update_curr` 消耗衰减，现场 dump 时 sum_weight、vruntime、各计数器看起来一切正常。**证据不在内存里，在代码与数值边界的推理里**：假设节点需要从“调度行为诡异”反推到“调度的算术基础可能坏了”，用 drgn 读出 `cfs_rq->sum_weight` 与实体权重，代入 place_entity 验证乘积上界，再走 UBSAN 复现闭环。

**双模式评估**：

1. **分析模式**（考 RCA）：输入 = 行为症状描述（低份额 cgroup 任务长睡唤醒后异常受宠/异常冷冻）+ 一份“看起来正常”的 vmcore。指标：定位准确率（标准答案即 `2f93bcb3f9aa`）；
2. **验证模式**（考修复闭环）：触发构造 + UBSAN oracle + 补丁验证。

**触发构造**（确定性，无竞态）：

```text
HZ=1000；单 CPU 绑定全部负载
根 cgroup: 114 × nice(-20) 自旋线程（cfs_rq->sum_weight ≈ 1.036e10）
cgroup A: cpu.shares=2（组实体 scaled weight 2048）
  └─ sleeper：浸润睡眠 ≥ 2.5h（vlag 达上界 891,289,600）
唤醒 sleeper → 组实体 place_entity：891289600 × 10361604098 > S64_MAX → 溢出
```

**oracle 草图**：

```json
{
  "oracle": {
    "type": "console_panic",
    "spec": { "pattern": "UBSAN: signed integer overflow.*place_entity" },
    "fail_when": "present"
  },
  "env": {
    "arch": "x86_64",
    "smp": 1,
    "memory_mb": 2048,
    "debug_config": "ubsan（signed-integer-overflow，Kconfig 细节开工时核对）",
    "extra_config": "HZ=1000"
  }
}
```

pattern 带函数名以过滤内核其他代码的 UBSAN 报告。

**验证参数**：算术必然而非竞态 → R_base = 1.0 → M=3，加阴性对照 3 次；每次运行含 ~2.5h 浸润，单 case 全程约 4 × 2.6h ≈ 过夜作业——这正好验证作业队列的长时间运行与断点续跑（预算按小时计）。

**沉淀的知识模板**（进 M3 知识库的第一条）：**行为诡异 + dump 干净 → 检查热路径的算术边界**（乘积溢出、移位、精度）。同类历史案例（vlag 上界裁剪、augmented tree 记账精度等）后续按此模板扩充。

---

## 后续扩充

- 每个 case 补一个 `case.yaml`（机器可读的触发/oracle/环境规格），供管线直接消费；
- sched_ext 系列（本批清单中的 UAF/stall/hotplug hang）是 M1 之后 KASAN/看门狗 oracle 的素材，触发需附带 BPF scheduler 程序，门槛一级；
- `118a2cf4ced6`（wakeup_preempt 类切换丢抢占，Paul McKenney 报告，2026-10-14 进 sched/urgent）：自建基准的下一个候选，原始触发绑定 rcutorture，需转化为纯用户态编排。
