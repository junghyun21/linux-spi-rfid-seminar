#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "mfrc522_ioctl.h"

#define MFRC522_DEVICE "/dev/mfrc5220"

int main(int argc, char *argv[])
{
    int fd;
    int ret;

    unsigned long sector;
    unsigned long block_num;

    struct mfrc522_block block;

    if (argc != 3) {
        fprintf(stderr,
                "Usage: %s <sector> <block>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    sector = strtoul(argv[1], NULL, 0);
    block_num = strtoul(argv[2], NULL, 0);

    if (sector > 15 || block_num > 3) {
        fprintf(stderr,
                "Invalid sector/block "
                "(0 <= sector <= 15, 0 <= block <= 3)\n");
        return EXIT_FAILURE;
    }

    block.sector = (uint8_t)sector;
    block.block  = (uint8_t)block_num;

    fd = open(MFRC522_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    while (1) {

        ret = ioctl(fd,
                    MFRC522_IOC_READ_BLOCK,
                    &block);

        if (ret == 0)
            break;

        if (errno == ETIMEDOUT ||
            errno == EIO ||
            errno == EBADMSG)
            continue;

        perror("MFRC522_IOC_READ_BLOCK");

        close(fd);
        return EXIT_FAILURE;
    }

    printf("Block Data = ");

    for (int i = 0; i < MFRC522_BLOCK_SIZE; i++)
        printf("%02X ", block.data[i]);

    printf("\n");

    close(fd);

    return EXIT_SUCCESS;
}