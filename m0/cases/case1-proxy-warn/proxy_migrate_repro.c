// SPDX-License-Identifier: GPL-2.0
/*
 * Case 1 触发器（内核模块）：proxy donors 的假迁移告警
 *
 * 对应修复: fe3c73d7bc76 ("sched/core: Avoid false migration warning for
 *           proxy donors")，基线 = 该提交的 parent（含 bug）。
 *
 * 机制（2026-10-22 对照 /home/yaqia/linux 提交正文与内核源码核实）：
 *   1. waiter 在 CPU0 上 migrate_disable() 后阻塞在内核 mutex 上；
 *      proxy execution 下阻塞任务留在 rq 上（is_blocked）
 *   2. CPU0 的 pick 路径进入 find_proxy_task()：owner 在 CPU1 上且
 *      runnable（on_rq）→ proxy_migrate_task() 把 waiter 的调度上下文
 *      迁到 CPU1 → set_task_cpu() 对 migration-disabled 任务 WARN
 *   3. owner 解锁 → try_to_wake_up() 送 waiter 回原 CPU（修复后此路径
 *      的 WARN 被 proxy_migrated 豁免）
 *
 * 两个必要条件（缺一不可，均经源码核实）：
 *   - owner 持锁期间必须 runnable（自旋而非睡眠）：
 *     find_proxy_task 对 !owner->on_rq 走 deactivate 分支，永不迁移
 *   - CONFIG_SCHED_PROXY_EXEC=y：与 CONFIG_SCHED_CLASS_EXT 编译期互斥
 *     （depends on !SCHED_CLASS_EXT && EXPERT），需关闭 sched_ext
 *
 * WARN 签名（checker pattern）："WARNING:.*set_task_cpu"
 *
 * 编译: 在基线内核树中 make M=<本目录> KBUILD_MODPOST_WARN=1
 *       （精简构建无 Module.symvers，符号校验交给加载器）
 * 运行: initramfs 内由 init 以 finit_module 加载（kdbg.kmod=/trigger.ko）
 */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/atomic.h>

static struct mutex lock;
static struct task_struct *owner_task;
static struct task_struct *waiter_task;
static atomic_t waiter_blocked = ATOMIC_INIT(0);

static int owner_fn(void *unused)
{
	mutex_lock(&lock);
	pr_info("pmr: owner 持锁于 CPU%d，自旋保持 runnable\n", smp_processor_id());
	/* 持锁期间必须 runnable：find_proxy_task 只在 owner on_rq 时
	 * 才把阻塞的 waiter 代理迁移过来（否则走 deactivate 分支） */
	while (!atomic_read(&waiter_blocked))
		cpu_relax();
	/* 给 CPU0 的 tick 留出触发代理迁移的时间（HZ=1000，1ms 一拍） */
	mdelay(200);
	pr_info("pmr: waiter task_cpu=%d（1 = 已代理迁移至 owner 的 CPU）\n",
		task_cpu(waiter_task));
	mutex_unlock(&lock);
	return 0;
}

static int waiter_fn(void *unused)
{
	pr_info("pmr: waiter 于 CPU%d migrate_disable 后阻塞\n", smp_processor_id());
	migrate_disable();
	atomic_set(&waiter_blocked, 1);
	mutex_lock(&lock);	/* 阻塞且留在 rq 上 → CPU0 代理迁移 → WARN */
	mutex_unlock(&lock);
	migrate_enable();
	pr_info("pmr: waiter 醒来于 CPU%d, task_cpu=%d\n",
		smp_processor_id(), task_cpu(current));
	return 0;
}

static int __init proxy_migrate_repro_init(void)
{
	mutex_init(&lock);

	owner_task = kthread_create(owner_fn, NULL, "pmr_owner");
	if (IS_ERR(owner_task))
		return PTR_ERR(owner_task);
	kthread_bind(owner_task, 1);
	wake_up_process(owner_task);

	msleep(100);

	waiter_task = kthread_create(waiter_fn, NULL, "pmr_waiter");
	if (IS_ERR(waiter_task))
		return PTR_ERR(waiter_task);
	kthread_bind(waiter_task, 0);
	wake_up_process(waiter_task);

	msleep(2000);
	pr_info("pmr: 编排完成\n");
	return 0;
}

static void __exit proxy_migrate_repro_exit(void)
{
	/* 线程已自行退出；kthread_stop 兼容已退出线程，实测校准时再收紧 */
	if (owner_task)
		kthread_stop(owner_task);
	if (waiter_task)
		kthread_stop(waiter_task);
}

module_init(proxy_migrate_repro_init);
module_exit(proxy_migrate_repro_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("kdebugent M0 case1: false migration warning for proxy donors");
