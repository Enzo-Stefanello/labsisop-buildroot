#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_LENGTH 1024

int main(int argc, char *argv[])
{
    int fd;
    ssize_t ret;
    char command[BUFFER_LENGTH];
    char receive[BUFFER_LENGTH];
    size_t used = 0;
    int i;

    if (argc != 8) {
        printf("Usage:\n");
        printf("%s <enc|dec> <key0> <key1> <key2> <key3> <data_size> <data>\n",
               argv[0]);
        return 1;
    }

    for (i = 1; i < argc; i++) {
        int written;

        written = snprintf(command + used,
                           sizeof(command) - used,
                           "%s%s",
                           (i == 1) ? "" : " ",
                           argv[i]);

        if (written < 0 ||
            (size_t)written >= sizeof(command) - used) {
            fprintf(stderr, "Command too long\n");
            return 1;
        }

        used += written;
    }

    printf("Invoking 'xtea_driver' system device.\n");
    printf("Command: %s\n", command);

    fd = open("/dev/xtea_driver", O_RDWR);

    if (fd < 0) {
        perror("Failed to open /dev/xtea_driver");
        return errno;
    }

    ret = write(fd, command, strlen(command));

    if (ret < 0) {
        perror("Failed to write command");
        close(fd);
        return errno;
    }

    ret = read(fd, receive, sizeof(receive) - 1);

    if (ret < 0) {
        perror("Failed to read result");
        close(fd);
        return errno;
    }

    receive[ret] = '\0';

    printf("Result: %s\n", receive);

    close(fd);

    return 0;
}
