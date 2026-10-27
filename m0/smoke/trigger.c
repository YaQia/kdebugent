/* m0/smoke/trigger.c — 阶段零冒烟触发器：只负责打印 checker 锚点 */
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	printf("kdbg: smoke-start pid=%d ncpu=%ld\n", getpid(),
	       sysconf(_SC_NPROCESSORS_ONLN));
	sleep(1);
	printf("kdbg: smoke-done rc=0\n");
	return 0;
}
