#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "spi_dev.h"
#include "MFRC522.h"

static int mfrc522_read_sector(uint8_t sector_addr, uint8_t data[4][16])
{
    int ret;

    for (int i = 0; i < 4; i++) {

        ret = mfrc522_read_block(sector_addr + i, data[i]);

        if (ret != MFRC522_OK) {
            // fprintf(stderr, "Block read failed\n");
            // fprintf(stderr, "[Read_block] Bring the card closer\n");
            mfrc522_command_cleanup();
            --i;
            continue;
        }
    }

    return MFRC522_OK;
}

/*
 * readSector
 * argv[0] = readSector
 * argv[1] = sector
 * argv[2] = NULL
 */
int main(int argc, char *argv[])
{
    uint8_t atqa[2];
    uint8_t data[4][16];
    uint8_t uid[7];
    uint8_t uid_len;

    uint8_t sector;
    uint8_t sector_addr;

    int ret;

    if(argc != 2) {
        fprintf(stderr, "Usage: %s <sector> <block>\n", argv[0]);
        return EXIT_FAILURE;
    }

    sector = (uint8_t)atoi(argv[1]);

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
    while (1) {

        ret = mfrc522_request(atqa);

        /* RF field 안에 응답한 PICC 없음 */
        if (ret == MFRC522_TIMEOUT) {
            continue;
        }

        /* Timeout 이외의 실제 통신 오류 */
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
        sector_addr = mifare_block_addr(sector, 0);  
    
        // MIFARE Authentication
        ret = mfrc522_authenticate(sector_addr, key_a, uid, uid_len);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "Authentication failed\n");
            // fprintf(stderr, "[Authentication] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }

        // 해당 Sector Authentication 후 Block 4개 READ
        ret = mfrc522_read_sector(sector_addr, data);
        if (ret != MFRC522_OK) {
            // fprintf(stderr, "Block read failed\n");
            // fprintf(stderr, "[Read_sector] Bring the card closer\n");
            mfrc522_command_cleanup();
            continue;
        }


        break;
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 16; j++) {
            printf("%02X ", data[i][j]);
        }

        if (i == 3) {
            printf("  <- Sector Trailer");
        }

        printf("\n");
    }

    mfrc522_cleanup();
    close(spi_fd);

    return EXIT_SUCCESS;
}