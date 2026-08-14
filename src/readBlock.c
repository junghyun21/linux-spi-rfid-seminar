#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "spi_dev.h"
#include "MFRC522.h"

/*
 * readBlock
 * argv[0] = "readBlock"
 * argv[1] = sector
 * argv[2] = block in sector
 * argv[3] = NULL
 */
int main(int argc, char *argv[])
{
    uint8_t atqa[2];
    uint8_t data[16];
    uint8_t uid[7];
    uint8_t uid_len;

    uint8_t sector;
    uint8_t block;
    uint8_t block_addr;

    int ret;

    if(argc != 3) {
        fprintf(stderr, "Usage: %s <sector> <block>\n", argv[0]);
        return EXIT_FAILURE;
    }

    sector = (uint8_t)atoi(argv[1]);
    block = (uint8_t)atoi(argv[2]);

    if (spi_init() < 0) {
        fprintf(stderr, "SPI init failed\n");
        return EXIT_FAILURE;
    }

    if (mfrc522_init() < 0) {
        fprintf(stderr, "MFRC522 init failed\n");
        close(spi_fd);
        return EXIT_FAILURE;
    }

    // PICC가 RF field에 들어올 때까지 REQA
    while(1) {
        ret = mfrc522_request(atqa);
        
        // 카드가 인식되지 않으면 루프 초기화 (RF field 안에 응답한 PICC 없음)
        if (ret == MFRC522_TIMEOUT) {
            continue;
        }

        //  timeout이 아닌 실제 통신 오류
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "REQA communication error\n");
            // fprintf(stderr, "[REQA] Bring the card closer\n");
            continue;
        }

        // 내부적으로 Anti-collision CL1 -> SELECT CL1 -> Anti-collision CL2 -> SELECT CL2 진행 완료
        ret = anticoll_and_select(uid, &uid_len);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "UID selection failed\n");
            // fprintf(stderr, "[anticoll & select] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }

        // 읽으려고 하는 곳의 주소
        block_addr = mifare_block_addr(sector, block);    // 읽을 곳의 주소

        // MIFARE Authentication
        ret = mfrc522_authenticate(block_addr, key_a, uid, uid_len);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "Authentication failed\n");
            // fprintf(stderr, "[Authentication] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }

        // 16-byte block READ
        ret = mfrc522_read_block(block_addr, data);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "Block read failed\n");
            // fprintf(stderr, "[Read_block] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }

        break;
    }

    printf("Block Data = ");
    for (int i = 0; i < 16; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");

    mfrc522_cleanup();
    close(spi_fd);

    return EXIT_SUCCESS;
}