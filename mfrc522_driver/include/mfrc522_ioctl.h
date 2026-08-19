#ifndef MFRC522_IOCTL_H
#define MFRC522_IOCTL_H

#include <linux/ioctl.h>
#include <linux/types.h>

/*
 * MFRC522 ioctl magic
 * 이 ioctl 명령들이 MFRC522용 명령 그룹이라는 걸 구분하는 식별자
 */
#define MFRC522_IOC_MAGIC    'M'


/*
 * 현재 기존 프로그램과 동일하게
 * 4-byte / 7-byte UID까지만 지원
 */
#define MFRC522_UID_MAX_LEN  7

/*
 * MIFARE Classic 1K
 */
#define MFRC522_BLOCK_SIZE       16
#define MFRC522_BLOCKS_PER_SECTOR 4


/*
 * UID 구조체
 */
struct mfrc522_uid {
    __u8 uid[MFRC522_UID_MAX_LEN];
    __u8 uid_len;
};


/*
 * Block 읽기
 *
 * userspace -> kernel
 *   sector
 *   block
 *
 * kernel -> userspace
 *   data[16]
 */
struct mfrc522_block {
    __u8 sector;
    __u8 block;

    __u8 data[MFRC522_BLOCK_SIZE];
};


/*
 * Sector 읽기
 *
 * userspace -> kernel
 *   sector
 *
 * kernel -> userspace
 *   data[4][16]
 */
struct mfrc522_sector {
    __u8 sector;

    __u8 data[MFRC522_BLOCKS_PER_SECTOR]
             [MFRC522_BLOCK_SIZE];
};


/*
 * UID:
 * kernel -> userspace
 */
#define MFRC522_IOC_READ_UID \
    _IOR(MFRC522_IOC_MAGIC, 0x01, struct mfrc522_uid)


/*
 * Block:
 * sector/block 입력 + data 출력
 */
#define MFRC522_IOC_READ_BLOCK \
    _IOWR(MFRC522_IOC_MAGIC, 0x02, struct mfrc522_block)


/*
 * Sector:
 * sector 입력 + data 출력
 */
#define MFRC522_IOC_READ_SECTOR \
    _IOWR(MFRC522_IOC_MAGIC, 0x03, struct mfrc522_sector)


#endif /* MFRC522_IOCTL_H */