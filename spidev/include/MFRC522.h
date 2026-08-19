#ifndef MFRC522_H
#define MFRC522_H

#include <stdio.h>
#include <stdint.h>
#include "PICC.h"

// register
#define CommandReg      0x01    // MFRC522 내부 명령 실행
#define ComIrqReg       0x04    
#define FIFODataReg     0x09    // 내부 64-byte FIFO 버퍼에 데이터를 입력/출력하기 위한 레지스터
#define FIFOLevelReg    0x0A
#define BitFramingReg   0x0D
#define TModeReg        0x2A
#define DivIrqReg       0x05
#define CRCResultRegH   0x21
#define CRCResultRegL   0x22
#define TModeReg        0x2A
#define TPrescalerReg   0x2B    // 1 tick의 시간 길이 결정
#define TReloadRegH     0x2C    // tick을 몇 번 샐지 결정
#define TReloadRegL     0x2D
#define Status2Reg      0x08
#define TxControlReg    0x14
#define TxModeReg       0x12
#define RxModeReg       0x13
#define ModWidthReg     0x24
#define TxASKReg        0x15
#define ErrorReg        0x06
#define TCounterValueRegH   0x2E
#define TCounterValueRegL   0x2F

// bit field
#define COMIRQ_TIMER    0x01
#define COMIRQ_IDLE     0x10
#define COMIRQ_RX       0x20
#define DIVIRQ_CRC      0x04
#define FIFO_FLUSH      0x80
#define START_SEND      0x80
#define TMODE_TAUTO     0x80
#define MFCRYPTO1ON     0x08
#define FORCE_100_ASK   0x40

// command
#define PCD_IDLE        0x00
#define PCD_TRANSMIT    0x04    // transmits data from the FIFO buffer
#define PCD_RECEIVE     0x08    
#define PCD_TRANSCEIVE  0x0C    // transmits data from FIFO buffer to antenna and automatically activates the receiver after transmission (with the BitFramingReg register’s StartSend bit)
#define PCD_MFAUTHENT   0x0E
#define PCD_SOFTRESET   0x0F
#define PCD_CALCCRC     0x03

// status
#define MFRC522_OK               0
#define MFRC522_ERR             -1
#define MFRC522_TIMEOUT         -2
#define MFRC522_COMM_ERROR      -3
#define MFRC522_BUFFER_ERROR    -4

// timeout (1tick ≈ 100µs)
#define REQA_TIMEOUT_TICKS      (REQA_TIMEOUT_MS / 100) 
#define ANTICOLL_TIMEOUT_TICKS  (ANTICOLL_TIMEOUT_MS / 100)
#define SELECT_TIMEOUT_TICKS    (SELECT_TIMEOUT_MS / 100)
#define READ_TIMEOUT_TICKS      (READ_TIMEOUT_MS / 100)
#define AUTH_TIMEOUT_TICKS      (AUTH_TIMEOUT_MS / 100)


uint8_t mfrc522_read_reg(uint8_t reg);
int mfrc522_write_reg(uint8_t reg, uint8_t value);

int mfrc522_init(void);
int mfrc522_cleanup(void);
int mfrc522_command_cleanup(void);

int mfrc522_request(uint8_t atqa[2]);
int anticoll_and_select(uint8_t uid[7], uint8_t *uid_len);
int mfrc522_authenticate(uint8_t block_addr, const uint8_t key[6], const uint8_t *uid, uint8_t uid_len);

int mfrc522_calculate_crc(const uint8_t *data, size_t len, uint8_t crc[2]);

int mfrc522_timer_init(void);
int mfrc522_timer_set(uint16_t ticks);

int mfrc522_read_block(uint8_t block_addr, uint8_t data[16]);
uint8_t mifare_block_addr(uint8_t sector, int8_t block_in_sector);

extern const uint8_t key_a[6];

#endif