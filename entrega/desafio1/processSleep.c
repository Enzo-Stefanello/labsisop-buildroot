#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/syscalls.h>
#include "processSleep.h"

asmlinkage long sys_listSleepingProcesses(char __user *buf, int size)
{
	struct task_struct *proces;
	char kbuf[4096];
	int used = 0;
	int ret;

	if (size <= 0) {
		return -1;
	}

	for_each_process(proces) {
		if (proces->state == TASK_INTERRUPTIBLE ||
		    proces->state == TASK_UNINTERRUPTIBLE) {

			int written;

			written = scnprintf(kbuf + used,
					    sizeof(kbuf) - used,
					    "PID: %ld\nProcess: %s\nState: %ld\n\n",
					    (long)task_pid_nr(proces),
					    proces->comm,
					    (long)proces->state);

			if (used + written >= sizeof(kbuf)) {
				return -1;
			}

			used += written;
		}
	}

	if (used + 1 > size) {
		return -1;
	}

	kbuf[used] = '\0';

	ret = copy_to_user((void *)buf, (void *)kbuf, used + 1);

	return (used + 1) - ret;
}
