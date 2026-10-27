// SPDX-License-Identifier: GPL-2.0
/*
 * m0/init/init.c — M0 通用 initramfs init（零依赖，静态编译）
 *
 * 职责：挂载基础文件系统 → 打开控制台 → 解析 kdbg.* 内核参数 →
 *       （可选）finit_module 加载内核模块触发器 →（可选）执行用户态触发器 →
 *       报告结果 → 关机。所有输出进 console，供宿主侧 checker 求值器匹配。
 *
 * cmdline 约定：
 *   kdbg.kmod=/trigger.ko    内核模块触发器（先加载，模块内自行编排并打印标记）
 *   kdbg.trig=/trigger       用户态触发器（后执行）
 *   kdbg.arg=...             传给触发器的单个参数（可选）
 *
 * console 输出约定（checker 锚点）：
 *   kdbg: init ready
 *   kdbg: kmod-rc=<n>        （加载了模块时）
 *   kdbg: trigger-rc=<n>     （执行了触发器时）
 *   kdbg: done
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __NR_finit_module
#define __NR_finit_module 313 /* x86_64 */
#endif

static char cl_buf[4096];

static const char *cmdline_val(const char *key)
{
	int fd = open("/proc/cmdline", O_RDONLY);
	if (fd < 0)
		return NULL;
	ssize_t n = read(fd, cl_buf, sizeof(cl_buf) - 1);
	close(fd);
	if (n <= 0)
		return NULL;
	cl_buf[n] = '\0';
	size_t klen = strlen(key);
	for (char *p = strtok(cl_buf, " \n"); p; p = strtok(NULL, " \n"))
		if (strncmp(p, key, klen) == 0 && p[klen] == '=')
			return p + klen + 1;
	return NULL;
}

static void copy_val(const char *key, char *dst, size_t len)
{
	const char *v = cmdline_val(key);
	if (v)
		snprintf(dst, len, "%s", v);
	else
		dst[0] = '\0';
}

static int load_module(const char *path)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	int rc = syscall(__NR_finit_module, fd, "", 0);
	close(fd);
	return rc;
}

int main(void)
{
	char kmod_path[256] = "", trig_path[256] = "", arg_str[256] = "";

	mkdir("/dev", 0755);
	mkdir("/proc", 0755);
	mkdir("/sys", 0755);
	mount("devtmpfs", "/dev", "devtmpfs", 0, NULL);
	mount("proc", "/proc", "proc", 0, NULL);
	mount("sysfs", "/sys", "sysfs", 0, NULL);

	int c = open("/dev/console", O_RDWR);
	if (c >= 0) {
		dup2(c, 0);
		dup2(c, 1);
		dup2(c, 2);
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("kdbg: init ready\n");

	copy_val("kdbg.kmod", kmod_path, sizeof(kmod_path));
	if (kmod_path[0]) {
		int rc = load_module(kmod_path);
		printf("kdbg: kmod-rc=%d (%s)\n", rc, kmod_path);
	}

	copy_val("kdbg.trig", trig_path, sizeof(trig_path));
	copy_val("kdbg.arg", arg_str, sizeof(arg_str));
	if (trig_path[0]) {
		pid_t pid = fork();
		if (pid == 0) {
			char *argv[3] = { trig_path, NULL, NULL };
			if (arg_str[0])
				argv[1] = arg_str;
			execve(trig_path, argv, NULL);
			printf("kdbg: exec %s 失败\n", trig_path);
			_exit(127);
		}
		int st = 0;
		waitpid(pid, &st, 0);
		printf("kdbg: trigger-rc=%d\n", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
	}

	printf("kdbg: done\n");
	sync();
	reboot(RB_POWER_OFF);
	return 0; /* 不可达 */
}
