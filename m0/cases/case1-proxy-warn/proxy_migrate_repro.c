// SPDX-License-Identifier: GPL-2.0
/*
 * Case 1 触发器（内核模块）：proxy donors 的假迁移告警
 *
 * 对应修复: fe3c73d7bc76 ("sched/core: Avoid false migration warning for
 *           proxy donors")，基线 = 该提交的 parent（含 bug）。
 *
 * 场景（依提交正文，2026-10-22 已对照 /home/yaqia/linux 核实）：
 *   CPU1: owner kthread 持有 mutex
 *   CPU0: waiter kthread migrate_disable() 后 mutex_lock() 阻塞
 *         → proxy execution 把 waiter 阻塞的调度上下文移到 owner 的 CPU1
 *   owner 解锁 → try_to_wake_up(waiter) → set_task_cpu() 对
 *   migration-disabled 任务无条件 WARN（修复前）：
 *     WARNING: kernel/sched/core.c:3389 at set_task_cpu+0x1d3/0x280
 *   oracle（console_panic 型）pattern: "WARNING:.*set_task_cpu"
 *
 * 环境: QEMU -smp 2；proxy execution 默认开启
 *   （DEFINE_STATIC_KEY_TRUE，无 config 门槛，boot 参数 sched_proxy_exec= 可控）。
 *
 * 编译: 在基线内核树（fe3c73d7bc76^ 检出）中 make M=<本目录>
 * 运行: 打入 initramfs，由 init 以 finit_module 加载（kdbg.kmod=/trigger.ko）
 *
 * 状态: 草稿——机制依提交正文编写，等待时长等时序参数需在基线上实测校准。
 */
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/delay.h>

static struct mutex lock;
static struct task_struct *owner_task;
static struct task_struct *waiter_task;

static int owner_fn(void *unused)
{
	mutex_lock(&lock);
	pr_info("pmr: owner 持锁于 CPU%d\n", smp_processor_id());
	msleep(500);
	pr_info("pmr: owner 解锁 → 唤醒 waiter（预期在此触发 WARN）\n");
	mutex_unlock(&lock);
	return 0;
}

static int waiter_fn(void *unused)
{
	pr_info("pmr: waiter 于 CPU%d migrate_disable 后阻塞\n", smp_processor_id());
	migrate_disable();
	mutex_lock(&lock);	/* 阻塞；proxy 迁移调度上下文至 owner 的 CPU */
	mutex_unlock(&lock);
	migrate_enable();
	pr_info("pmr: waiter 醒来于 CPU%d\n", smp_processor_id());
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
