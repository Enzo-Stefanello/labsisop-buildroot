#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

#define SYSCALL_PRINT_MESSAGE 387

void usage(char *program)
{
    printf("Usage: %s <message>\n", program);
    exit(1);
}

int main(int argc, char **argv)
{
    long ret;

    if (argc < 2) {
        usage(argv[0]);
    }

    printf("Invoking 'printMessage' system call.\n");

    ret = syscall(SYSCALL_PRINT_MESSAGE, argv[1]);

    if (ret >= 0) {
        printf("Message sent to kernel log.\n");
    } else {
        printf("System call 'printMessage' failed, error %ld\n", ret);
    }

    return 0;
}
