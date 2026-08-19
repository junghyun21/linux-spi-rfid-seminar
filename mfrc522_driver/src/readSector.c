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

    unsigned long sector_num;

    struct mfrc522_sector sector;

    if (argc != 2) {
        fprintf(stderr,
                "Usage: %s <sector>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    sector_num = strtoul(argv[1], NULL, 0);

    if (sector_num > 15) {
        fprintf(stderr,
                "Invalid sector "
                "(0 <= sector <= 15)\n");
        return EXIT_FAILURE;
    }

    sector.sector = (uint8_t)sector_num;

    fd = open(MFRC522_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    while (1) {

        ret = ioctl(fd,
                    MFRC522_IOC_READ_SECTOR,
                    &sector);

        if (ret == 0)
            break;

        if (errno == ETIMEDOUT ||
            errno == EIO ||
            errno == EBADMSG)
            continue;

        perror("MFRC522_IOC_READ_SECTOR");

        close(fd);
        return EXIT_FAILURE;
    }

    for (int i = 0;
         i < MFRC522_BLOCKS_PER_SECTOR;
         i++) {

        for (int j = 0;
             j < MFRC522_BLOCK_SIZE;
             j++) {

            printf("%02X ", sector.data[i][j]);
        }

        if (i == MFRC522_BLOCKS_PER_SECTOR - 1)
            printf(" <- Sector Trailer");

        printf("\n");
    }

    close(fd);

    return EXIT_SUCCESS;
}