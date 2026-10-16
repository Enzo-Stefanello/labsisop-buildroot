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

    if (argc != 4) {
        printf("Usage: %s <enc|dec> <data_size> <data>\n",
               argv[0]);
        return 1;
    }

    snprintf(command,
             sizeof(command),
             "%s %s %s",
             argv[1],
             argv[2],
             argv[3]);

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

    {
        char receive[BUFFER_LENGTH];

        ret = read(fd, receive, sizeof(receive) - 1);

        if (ret < 0) {
            perror("Failed to read result");
            close(fd);
            return errno;
        }

        receive[ret] = '\0';

        printf("Result: %s\n", receive);
    }

    close(fd);

    return 0;
}
