#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include "MFRC522.h"
#include "spi_dev.h"

const uint8_t key_a[6] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};

// bit -> 1
static int set_bits(uint8_t reg, uint8_t mask)
{
    uint8_t value;

    value = mfrc522_read_reg(reg);
    value |= mask;

    return mfrc522_write_reg(reg, value);
}

// bit -> 0
static int clear_bits(uint8_t reg, uint8_t mask)
{
    uint8_t value;

    value = mfrc522_read_reg(reg);
    value &= (uint8_t)~mask;

    return mfrc522_write_reg(reg, value);
}

static int mfrc522_soft_reset(void)
{
    if (mfrc522_write_reg(CommandReg, PCD_SOFTRESET) < 0) {
        return MFRC522_ERR;
    }

    usleep(5000);

    return MFRC522_OK;
}

static int mfrc522_transceive(const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t *rx_len)
{
    uint8_t irq;
    uint8_t fifo_level;

    // 현재 수행 중인 명령을 Idle 상태로 전환 (Command[3:0] 외의 다른 필드의 default 값 = 0)
    if (mfrc522_write_reg(CommandReg, PCD_IDLE) < 0) {
        return MFRC522_ERR;
    }

    // 이전 IRQ 상태 제거
    // ComIrqReg's Set1(7) = 1 : [6:0] 중 1로 지정한 비트를 set
    // ComIrqReg's Set1(6) = 0 : [6:0] 중 1로 지정한 비트를 clear
    if (mfrc522_write_reg(ComIrqReg, 0x7F) < 0) {
        return MFRC522_ERR;
    }

    // FIFO 초기화: FlushBuffer[7] = 1 -> FIFO 자동 flush x
    // FIFOLevel[6:0]는 read-only
    if (mfrc522_write_reg(FIFOLevelReg, FIFO_FLUSH) < 0) {
        return MFRC522_ERR;
    }

    // PICC에 송신할 데이터를 FIFO에 저장
    for (size_t i = 0; i < tx_len; i++) {
        if (mfrc522_write_reg(FIFODataReg, tx[i]) < 0) {
            return MFRC522_ERR;
        }
    }

    // 내부 64-byte FIFO 명령 설정
    // 모든 명령은 입력 즉시 실행 <but> transceive 실행 -> BitFramingReg의 StartSend(bit7) 설정 즉시 실행
    if (mfrc522_write_reg(CommandReg, PCD_TRANSCEIVE) < 0) {
        return MFRC522_ERR;
    }

    if (set_bits(BitFramingReg, START_SEND) < 0) {
        return MFRC522_ERR;
    }

    // PICC 응답 또는 Timer 만료까지 대기
    while(1) {
        irq = mfrc522_read_reg(ComIrqReg);

        // ComIrqReg'S TimerIRq(0) = 1 -> count가 0에 도달한 상태
        if(irq & COMIRQ_TIMER) {
            return MFRC522_TIMEOUT;
        }

        // ComIrqReg'S RxIRq(5) = 1 -> 수신을 끝마친 상태
        if (irq & (COMIRQ_RX)) {
            break;
        }
    }

    // 수신된 byte 수 확인
    // 사용자가 넘겨준 rx buffer보다 수신 데이터가 크면 buffer overflow 방지
    fifo_level = mfrc522_read_reg(FIFOLevelReg);

    if (fifo_level > *rx_len) {
        return MFRC522_BUFFER_ERROR;
    }

    // FIFO에서 실제 PICC의 응답 읽기
    for (uint8_t i = 0; i < fifo_level; i++) {
        rx[i] = mfrc522_read_reg(FIFODataReg);
    }

    *rx_len = fifo_level;

    return MFRC522_OK;
}

static int mfrc522_anticoll(uint8_t sel, uint8_t uid_part[5])
{
    uint8_t tx[2];
    size_t rx_len = 5;
    int ret;

    tx[0] = sel;
    tx[1] = PICC_ANTICOLL;    // 유효한 full byte = 2개

    // Anti-collision은 byte 단위 통신 -> TxLastBits = 0b000 // 8bit 전체
    if (mfrc522_write_reg(BitFramingReg, 0x00) < 0) {
        return MFRC522_ERR;
    }

    // Anti-collision 응답 timeout 설정
    if (mfrc522_timer_set(ANTICOLL_TIMEOUT_TICKS) < 0) {
        return MFRC522_ERR;
    }

    // uid_part: PICC가 보내주는 응답(cascade Tag, UID, BCC)이 채워질 예정
    ret = mfrc522_transceive(
            tx,
            2,
            uid_part,
            &rx_len);

    if (ret != MFRC522_OK) {
        return ret;
    }

    // Anti-collision 응답은 5 byte (UID CLn 4 byte + BCC 1 byte)
    if (rx_len != 5) {
        return MFRC522_COMM_ERROR;
    }

    // BCC 검사
    if ((uid_part[0] ^ uid_part[1] ^ uid_part[2] ^ uid_part[3]) != uid_part[4]) {
        return MFRC522_COMM_ERROR;
    }

    return MFRC522_OK;
}

static int mfrc522_select(uint8_t sel, const uint8_t uid_part[5], uint8_t *sak)
{
    uint8_t tx[9];      // SEL(1byte) + NVB(1byte) + 이미 알고 있는 UID 일부/전체(0~40bit(5yte)) + CRC_A(2byte)
    uint8_t rx[3];      // SAK(1byte) + CRC_A(2byte)
    uint8_t crc[2];

    size_t rx_len = sizeof(rx);
    int ret;

    // SELECT cascade level n (uid_part) frame 구성
    tx[0] = sel; 
    tx[1] = PICC_SELECT;

    tx[2] = uid_part[0];
    tx[3] = uid_part[1];
    tx[4] = uid_part[2];
    tx[5] = uid_part[3];
    tx[6] = uid_part[4];

    // 앞 7 byte에 대해 CRC_A 계산
    ret = mfrc522_calculate_crc(tx, 7, crc);

    if (ret != MFRC522_OK) {
        return ret;
    }

    tx[7] = crc[0];
    tx[8] = crc[1];

    // SELECT는 일반 byte frame
    if (mfrc522_write_reg(BitFramingReg, 0x00) < 0) {
        return MFRC522_ERR;
    }

    // SELECT 응답 timeout 설정
    if (mfrc522_timer_set(SELECT_TIMEOUT_TICKS) < 0) {
        return MFRC522_ERR;
    }

    // SELECT frame 전송
    ret = mfrc522_transceive(
            tx,
            sizeof(tx),
            rx,
            &rx_len);

    if (ret != MFRC522_OK) {
        return ret;
    }

    // PICC SELECT 응답 = SAK(1byte) + CRC_A(2byte)
    if (rx_len != 3) {
        return MFRC522_COMM_ERROR;
    }

    ret = mfrc522_calculate_crc(rx, 1, crc);

    if (ret != MFRC522_OK) {
        return ret;
    }

    if (rx[1] != crc[0] || rx[2] != crc[1]) {
        return MFRC522_COMM_ERROR;
    }

    *sak = rx[0];

    return MFRC522_OK;
}

uint8_t mfrc522_read_reg(uint8_t reg)
{
    uint8_t tx[2];
    uint8_t rx[2];

    /*
     * MFRC522 SPI read format
     *
     * bit | 7 | 6 5 4 3 2 1 | 0
     * reg | 1 |   address   | 0
     */
    tx[0] = ((reg << 1) & 0x7E) | 0x80;     // read bit + address
    tx[1] = 0x00;                           // dump

    rx[0] = 0;
    rx[1] = 0;

    if (spi_transfer(tx, rx, 2) < 0) {
        return 0;
    }

    return rx[1];
}

int mfrc522_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[2];
    uint8_t rx[2];

    /*
     * MFRC522 SPI write format
     *
     * bit | 7 | 6 5 4 3 2 1 | 0
     * reg | 0 |   address   | 0
     */
    tx[0] = (reg << 1) & 0x7E;      // write bit + address
    tx[1] = value;                  // data

    rx[0] = 0;
    rx[1] = 0;

    if (spi_transfer(tx, rx, 2) < 0)
        return -1;

    return 0;
}

int mfrc522_init(void)
{
    // resets the MFRC522
    if (mfrc522_soft_reset() < 0) {
        return MFRC522_ERR;
    }

    // MFRC522 Timer 초기화 - TAuto 설정, Timer tick 길이 설정
    if(mfrc522_timer_init() < 0) {
        fprintf(stderr, "timer init failed\n");
        return MFRC522_ERR;
    }

    /* RF antenna driver ON */
    if (set_bits(TxControlReg, 0x03) < 0) {
        fprintf(stderr, "antenna driver ON failed\n");
        return MFRC522_ERR;
    }

    /* ISO14443A용 100% ASK */
    if (set_bits(TxASKReg, FORCE_100_ASK) < 0) {
        fprintf(stderr, "ASK init failed\n");
        return MFRC522_ERR;
    }

    usleep(5000);

    return MFRC522_OK;
}

int mfrc522_cleanup(void)
{
    if (mfrc522_command_cleanup() < 0) {
        return MFRC522_ERR;
    }

    // 프로그램 종료 시 antenna OFF
    if (clear_bits(TxControlReg, 0x03) < 0) {
        return MFRC522_ERR;
    }

    return MFRC522_OK;
}

int mfrc522_command_cleanup(void)
{
    // Crypto1 해제 -> Status2Reg's MFCrtpto1On(3) = 0
    if(clear_bits(Status2Reg, MFCRYPTO1ON) < 0) {
        return MFRC522_ERR;
    }

    // 현재 command 종료
    if (mfrc522_write_reg(CommandReg, PCD_IDLE) < 0) {
        return MFRC522_ERR;
    }

    // FIFO 비우기
    if (mfrc522_write_reg(FIFOLevelReg, FIFO_FLUSH) < 0) {
        return MFRC522_ERR;
    }

    // 이전 IRQ clear
    if (mfrc522_write_reg(ComIrqReg, 0x7F) < 0) {
        return MFRC522_ERR;
    }

    return MFRC522_OK;
}

int mfrc522_request(uint8_t atqa[2])
{
    uint8_t reqa = PICC_REQA;
    size_t rx_len = 2;
    int ret;

    // REQA 1회에 대한 응답 대기시간 설정
    if (mfrc522_timer_set(REQA_TIMEOUT_TICKS) < 0) {
        return MFRC522_ERR;
    }

    // REQA는 7-bit short frame
    // BitFramingReg.TxLastBits = 7 -> FIFO에서 마지막 7bit만 송신 [6:0]
    if (mfrc522_write_reg(BitFramingReg, 0x07) < 0) {
        return MFRC522_ERR;
    }

    // REQA 전송 후 ATQA 수신
    ret = mfrc522_transceive(
            &reqa,
            1,
            atqa,
            &rx_len);

    if (ret != MFRC522_OK) {
        return ret;
    }

    // ATQA는 정확히 2 byte
    if (rx_len != 2) {
        return MFRC522_COMM_ERROR;
    }

    return MFRC522_OK;
}

// MFAuthent Command - 메모리 접근 전 이 섹터에 접근할 권한이 있는지 확인하는 단계
// byte  |            1             |     2      |       3        | ... |       8       |   9    | ... |   12   |
// value | Auth command(0x60, 0x61) | Block Addr | sector key [0] | ... | sector key[5] | UID[0] | ... | UID[3] |
int mfrc522_authenticate(uint8_t block_addr, const uint8_t key[6], const uint8_t *uid, uint8_t uid_len)
{
    uint8_t auth[12];
    uint8_t irq;

    if (uid == NULL) {
        return MFRC522_ERR;
    }

    if (uid_len != 4 && uid_len != 7) {
        return MFRC522_ERR;
    }

    auth[0] = PICC_MF_AUTH_KEY_A;
    auth[1] = block_addr;

    for (int i = 0; i < 6; i++) {
        auth[2 + i] = key[i];
    }

    // UID의 마지막 4 bytes 사용
    auth[8]  = uid[uid_len - 4];
    auth[9]  = uid[uid_len - 3];
    auth[10] = uid[uid_len - 2];
    auth[11] = uid[uid_len - 1];

    // 이전 command 종료 및 FIFO 비우기
    if (mfrc522_write_reg(CommandReg, PCD_IDLE) < 0) {
        return MFRC522_ERR;
    }

    if (mfrc522_write_reg(FIFOLevelReg, FIFO_FLUSH) < 0) {
        return MFRC522_ERR;
    }

    // 이전 IRQ 상태 제거
    // ComIrqReg's Set1(7) = 1 : [6:0] 중 1로 지정한 비트를 set
    // ComIrqReg's Set1(6) = 0 : [6:0] 중 1로 지정한 비트를 clear
    if (mfrc522_write_reg(ComIrqReg, 0x7F) < 0) {
        return MFRC522_ERR;
    }

    // 인증 frame을 FIFO에 저장
    for (int i = 0; i < 12; i++) {
        if (mfrc522_write_reg(FIFODataReg, auth[i]) < 0) {
            return MFRC522_ERR;
        }
    }

    // 인증 timeout 설정
    if (mfrc522_timer_set(AUTH_TIMEOUT_TICKS) < 0) {
        return MFRC522_ERR;
    }

    // MIFARE authentication 실행
    if (mfrc522_write_reg(CommandReg, PCD_MFAUTHENT) < 0) {
        return MFRC522_ERR;
    }

    // Authentication 완료 대기
    while (1) {
        irq = mfrc522_read_reg(ComIrqReg);

        if (irq & COMIRQ_TIMER) {
            return MFRC522_TIMEOUT;
        }

        if (irq & COMIRQ_IDLE) {
            break;
        }
    }

    // 실제 Crypto1 활성화 여부 확인
    // MFAuthent 명령이 성공적으로 실행된 경우 -> Status2Reg's MFCrypto1On 1로 설정됨
    if (!(mfrc522_read_reg(Status2Reg) & MFCRYPTO1ON)) {
        return MFRC522_COMM_ERROR;
    }

    return MFRC522_OK;
}

int anticoll_and_select(uint8_t uid[7], uint8_t *uid_len)
{
    uint8_t cl1[5];     // cascade level 1
    uint8_t cl2[5];     // cascade level 2
    uint8_t sak;
    int ret;

    if (uid == NULL || uid_len == NULL) {
        return MFRC522_ERR;
    }

    ret = mfrc522_anticoll(PICC_SEL_CL1, cl1);
    if (ret != MFRC522_OK) {
        return ret;
    }

    // 4-byte UID 
    // | byte  |  1   |  2   |  3   |  4   |  5  |
    // | value | UID0 | UID1 | UID2 | UID3 | BCC |
    if (cl1[0] != PICC_CT) {
        uid[0] = cl1[0];
        uid[1] = cl1[1];
        uid[2] = cl1[2];    
        uid[3] = cl1[3];

        ret = mfrc522_select(PICC_SEL_CL1, cl1, &sak);
        if (ret != MFRC522_OK) {
            return ret;
        }

        // 4-byte UID이면 CL1에서 UID가 끝나므로 Cascade bit = 0
        if (sak & SAK_CASCADE_BIT) {
            return MFRC522_COMM_ERROR;
        }

        *uid_len = 4;

        return MFRC522_OK;
    }

    // Cascade Level 1
    // | byte  |        1        |  2   |  3   |  4   |  5  |
    // | value | Cascade Tag(CT) | UID1 | UID2 | UID3 | BCC |
    uid[0] = cl1[1];
    uid[1] = cl1[2];
    uid[2] = cl1[3];    

    ret = mfrc522_select(PICC_SEL_CL1, cl1, &sak);
    if (ret != MFRC522_OK) {
        return ret;
    }

    // Cascade bit = 1 -> 다음 Cascade Level 존재
    if (!(sak & SAK_CASCADE_BIT)) {
        return MFRC522_COMM_ERROR;
    }

    // Cascade Level 2
    // | byte  |  1   |  2   |  3   |  4   | 
    // | value | UID4 | UID5 | UID6 | UID7 | 
    ret = mfrc522_anticoll(PICC_SEL_CL2, cl2);
    if (ret != MFRC522_OK) {
        return ret;
    }

    uid[3] = cl2[0];
    uid[4] = cl2[1];
    uid[5] = cl2[2];
    uid[6] = cl2[3];

    ret = mfrc522_select(PICC_SEL_CL2, cl2, &sak);
    if (ret != MFRC522_OK) {
        return ret;
    }

    // 7-byte UID라면 CL2에서 끝나야 함 (Cascade bit가 또 1이면 CL3가 필요한 10-byte UID라는 의미)
    if (sak & SAK_CASCADE_BIT) {
        return MFRC522_COMM_ERROR;
    }

    *uid_len = 7;

    return MFRC522_OK;
}

int mfrc522_calculate_crc(const uint8_t *data, size_t len, uint8_t crc[2])
{
    uint8_t irq;

    // 현재 command 종료
    if (mfrc522_write_reg(CommandReg, PCD_IDLE) < 0) {
        return MFRC522_ERR;
    }

    // 이전 IRQ 상태 제거
    // ComIrqReg's Set1(7) = 1 : [6:0] 중 1로 지정한 비트를 set
    // ComIrqReg's Set1(6) = 0 : [6:0] 중 1로 지정한 비트를 clear
    if (mfrc522_write_reg(ComIrqReg, 0x7F) < 0) {
        return MFRC522_ERR;
    }

   // FIFO 초기화
    if (mfrc522_write_reg(FIFOLevelReg, FIFO_FLUSH) < 0) {
        return MFRC522_ERR;
    }

    // 이전 CRC interrupt flag clear -> DivIrqReg의 CRCIRq는 1을 쓰면 clear
    if (mfrc522_write_reg(DivIrqReg, DIVIRQ_CRC) < 0) {
        return MFRC522_ERR;
    }

    // CRC 계산 대상 데이터를 FIFO에 넣음
    for (size_t i = 0; i < len; i++) {
        if (mfrc522_write_reg(FIFODataReg, data[i]) < 0)
            return MFRC522_ERR;
    }

    // CRC 계산 시작
    if (mfrc522_write_reg(CommandReg, PCD_CALCCRC) < 0) {
        return MFRC522_ERR;
    }

    // CRC 계산 대기
    while (1) {
        irq = mfrc522_read_reg(DivIrqReg);

        // DivIrqReg's CRCIRq(2) = 1 -> CalcCRC 명령어 활성화 & 모든 데이터 처리 완료
        if (irq & DIVIRQ_CRC) {
            break;
        }
    }

    // 계산 결과 읽기 -> RF frame에는 Low byte 먼저 보냄
    crc[0] = mfrc522_read_reg(CRCResultRegL);
    crc[1] = mfrc522_read_reg(CRCResultRegH);

    return MFRC522_OK;
}

int mfrc522_timer_init(void)
{
    uint8_t tmode;

    // TAuto(7) = 1 : 송신이 끝나면 자동으로 timer 시작
    if (mfrc522_write_reg(TModeReg, TMODE_TAUTO) < 0) {
        return -1;
    }

    // Timer prescaler 설정 -> ftimer = 13.56 MHz / (2*TPreScaler+1) : 1 tick의 길이
    // TPreScaler ≈ 677.5 (678) -> (2 * 677.5 + 1) / 13.56 MHz ≈ 100µs
    tmode = mfrc522_read_reg(TModeReg);
    tmode &= ~0x0f;                                 // bit[3:0]만 0으로 클리어
    tmode |= 0x02;                                  // TPrescaler_Hi = 0x2

    if (mfrc522_write_reg(TModeReg, tmode) < 0) {
        return -1;
    }
    if (mfrc522_write_reg(TPrescalerReg, 0xA6) < 0) {
        return -1;
    }

    return 0;
}

int mfrc522_timer_set(uint16_t ticks)
{
    // Timer reload value -> tick의 개수 설정
    if (mfrc522_write_reg(TReloadRegH, (ticks >> 8) & 0xFF) < 0) {
        return -1;
    }

    if (mfrc522_write_reg(TReloadRegL, ticks & 0xFF) < 0) {
        return -1;
    }

    return 0;
}

// read Command
// ----------------     tx     ----------------
// byte  |    1      |     2      |  3  |  4  |
// value | Cmd(0x30) | Block Addr | CRC | CRC |
// ----------------     Rx     ----------------
// byte  |    1    | ... |    16    |  17 |  18 |
// value | Data[0] | ... | Data[15] | CRC | CRC |
int mfrc522_read_block(uint8_t block_addr, uint8_t data[16])
{
    uint8_t tx[4];
    uint8_t rx[18];
    uint8_t crc[2];

    size_t rx_len = sizeof(rx);
    int ret;

    tx[0] = PICC_READ;     // 0x30
    tx[1] = block_addr;

    // 30 + Block Address에 대한 CRC_A
    ret = mfrc522_calculate_crc(tx, 2, crc);
    if (ret != MFRC522_OK) {
        return ret;
    }

    tx[2] = crc[0];
    tx[3] = crc[1];

    // 일반 byte frame
    if (mfrc522_write_reg(BitFramingReg, 0x00) < 0) {
        return MFRC522_ERR;
    }

    if (mfrc522_timer_set(READ_TIMEOUT_TICKS) < 0) {
        return MFRC522_ERR;
    }

    // READ command 전송
    ret = mfrc522_transceive(tx, sizeof(tx), rx, &rx_len);
    if (ret != MFRC522_OK) {
        return ret;
    }

    // Read 응답
    if (rx_len != 18) {
        return MFRC522_COMM_ERROR;
    }

    // 수신 데이터 CRC 검증
    ret = mfrc522_calculate_crc(rx, 16, crc);
    if (ret != MFRC522_OK) {
        return ret;
    }

    if (rx[16] != crc[0] || rx[17] != crc[1]) {
        return MFRC522_COMM_ERROR;
    }

    // 실제 Block 데이터 16 bytes만 복사
    for (int i = 0; i < 16; i++) {
        data[i] = rx[i];
    }

    /*
     * MIFARE Classic 1K:
     * 각 sector의 마지막 block(block % 4 == 3)은 Sector Trailer
     *
     * Key A는 READ 명령으로 읽을 수 없기 때문에
     * 카드 응답에서는 data[0..5]가 00으로 반환된다.
     *
     * 단, 이 프로그램은 Authentication에 사용한 Key A를 알고 있으므로
     * dump 형태로 보여주기 위해 알려진 Key A를 채운다.
     */
    if ((block_addr % 4) == 3) {

        for (int i = 0; i < 6; i++) {
            data[i] = key_a[i];
        }
    }

    return MFRC522_OK;
}

uint8_t mifare_block_addr(uint8_t sector, int8_t block_in_sector)
{
    return sector * 4 + block_in_sector;
}