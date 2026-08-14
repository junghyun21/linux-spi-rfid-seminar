#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "spi_dev.h"
#include "MFRC522.h"

/*
 * readUID
 * argv[0] = "readBlock"
 * argv[1] = NULL
 */
int main(void)
{
    uint8_t atqa[2];
    uint8_t uid[7];
    uint8_t uid_len;
    int ret;

    if (spi_init() < 0) {
        fprintf(stderr, "SPI init failed\n");
        return EXIT_FAILURE;
    }

    if (mfrc522_init() < 0) {
        fprintf(stderr, "MFRC522 init failed\n");
        close(spi_fd);
        return EXIT_FAILURE;
    }

    // PICC가 RF field에 들어올 때까지 REQA 반복
    while (1) {
        ret = mfrc522_request(atqa);

        if (ret == MFRC522_TIMEOUT) {
            continue;
        }

        if (ret != MFRC522_OK) {
            // fprintf(stderr, "REQA communication error\n");
            // fprintf(stderr, "[REQA] Bring the card closer\n");
            continue;
        }
        
        // UID 획득 + PICC SELECT 완료
        // 내부적으로 Anti-collision CL1 -> SELECT CL1 -> Anti-collision CL2 -> SELECT CL2 진행 완료
        ret = anticoll_and_select(uid, &uid_len);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "UID selection failed\n");
            // fprintf(stderr, "[anticoll & select] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }

        break;
    }

    printf("UID = ");
    for (int i = 0; i < uid_len; i++) {
        printf("%02X ", uid[i]);
    }
    printf("\n");

    mfrc522_cleanup();
    close(spi_fd);

    return EXIT_SUCCESS;
}