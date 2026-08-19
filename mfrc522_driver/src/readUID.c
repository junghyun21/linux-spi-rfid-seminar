#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "mfrc522_ioctl.h"

#define MFRC522_DEVICE "/dev/mfrc5220"

int main(void)
{
    int fd;
    int ret;

    struct mfrc522_uid uid;

    fd = open(MFRC522_DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    /*
     * PICC가 RF field 안에 들어올 때까지 반복.
     *
     * 기존 프로그램에서도 REQA timeout이면
     * 계속 반복했으므로 동일한 사용 방식 유지.
     */
    while (1) {
        ret = ioctl(fd, MFRC522_IOC_READ_UID, &uid);

        if (ret == 0)
            break;

        /*
         * 카드가 없는 경우
         */
        if (errno == ETIMEDOUT)
            continue;

        /*
         * RFID 통신 과정에서 일시적인 오류가 발생한 경우
         * 다시 카드 선택을 시도.
         */
        if (errno == EIO ||
            errno == EBADMSG)
            continue;

        perror("MFRC522_IOC_READ_UID");

        close(fd);
        return EXIT_FAILURE;
    }

    printf("UID = ");

    for (int i = 0; i < uid.uid_len; i++)
        printf("%02X ", uid.uid[i]);

    printf("\n");

    close(fd);

    return EXIT_SUCCESS;
}