#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include "printMessage.h"

asmlinkage long sys_printMessage(const char __user *buf)
{
    char kbuf[256];
    long ret;

    ret = strncpy_from_user(kbuf, buf, sizeof(kbuf) - 1);

    if (ret < 0) {
        return ret;
    }

    kbuf[ret] = '\0';

    printk(KERN_INFO "sys_printMessage: %s\n", kbuf);

    return ret;
}
