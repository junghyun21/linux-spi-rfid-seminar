// SPDX-License-Identifier: GPL-2.0

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/bitmap.h>

#include <linux/of.h>
#include <linux/spi/spi.h>

#include "mfrc522_ioctl.h"


/* ============================================================
 * Driver configuration
 * ============================================================
 */

#define MFRC522_NAME           "mfrc522"

#define N_MFRC522_MINORS       32


/* ============================================================
 * MFRC522 Register
 * ============================================================
 */

#define CommandReg             0x01
#define ComIrqReg              0x04
#define DivIrqReg              0x05
#define ErrorReg               0x06
#define Status2Reg             0x08
#define FIFODataReg            0x09
#define FIFOLevelReg           0x0A
#define BitFramingReg          0x0D

#define TxModeReg              0x12
#define RxModeReg              0x13
#define TxControlReg           0x14
#define TxASKReg               0x15

#define CRCResultRegH          0x21
#define CRCResultRegL          0x22

#define ModWidthReg            0x24

#define TModeReg               0x2A
#define TPrescalerReg          0x2B
#define TReloadRegH            0x2C
#define TReloadRegL            0x2D

#define VersionReg             0x37


/* ============================================================
 * MFRC522 bit fields
 * ============================================================
 */

#define COMIRQ_TIMER           0x01
#define COMIRQ_IDLE            0x10
#define COMIRQ_RX              0x20

#define DIVIRQ_CRC             0x04

#define FIFO_FLUSH             0x80
#define START_SEND             0x80

#define TMODE_TAUTO            0x80
#define MFCRYPTO1ON            0x08
#define FORCE_100_ASK          0x40


/* ============================================================
 * MFRC522 Commands
 * ============================================================
 */

#define PCD_IDLE               0x00
#define PCD_CALCCRC            0x03
#define PCD_TRANSCEIVE         0x0C
#define PCD_MFAUTHENT          0x0E
#define PCD_SOFTRESET          0x0F


/* ============================================================
 * PICC Commands
 * ============================================================
 */

#define PICC_REQA              0x26
#define PICC_READ              0x30
#define PICC_ANTICOLL          0x20
#define PICC_SELECT            0x70

#define PICC_MF_AUTH_KEY_A     0x60

#define PICC_CT                0x88
#define PICC_SEL_CL1           0x93
#define PICC_SEL_CL2           0x95

#define SAK_CASCADE_BIT        0x04


/* ============================================================
 * Timeout
 *
 * 기존 프로그램의 timer 설정과 동일한 값 사용
 * timer tick ≈ 100 us
 * ============================================================
 */

#define REQA_TIMEOUT_TICKS      50
#define ANTICOLL_TIMEOUT_TICKS  50
#define SELECT_TIMEOUT_TICKS    50
#define READ_TIMEOUT_TICKS      50
#define AUTH_TIMEOUT_TICKS      10


/*
 * 기존 프로그램에서 사용하던 MIFARE Classic 기본 Key A
 */
static const u8 key_a[6] = {
    0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF
};


/* ============================================================
 * Per-device private data
 *
 * spidev의 struct spidev_data에 대응
 * ============================================================
 */

struct mfrc522_data {
    dev_t devt;

    /*
     * SPI device 제거와 SPI operation 간 동기화
     */
    struct mutex spi_lock;

    /*
     * DT를 통해 만들어지고 이 driver에 binding된
     * 실제 SPI target
     */
    struct spi_device *spi;

    /*
     * open()에서 devt를 이용해 device를 찾기 위한 list
     */
    struct list_head device_entry;

    unsigned int users;
};


/* ============================================================
 * Character device management
 * ============================================================
 */

static int mfrc522_major;

static DECLARE_BITMAP(minors, N_MFRC522_MINORS);

static LIST_HEAD(mfrc522_device_list);

static DEFINE_MUTEX(mfrc522_device_list_lock);


/*
 * spidev_class와 동일한 목적.
 *
 * udev가 /dev/mfrc522X를 만들 수 있도록
 * character-device class를 제공한다.
 */
static const struct class mfrc522_class = {
    .name = "mfrc522",
};


/* ============================================================
 * Low-level SPI transfer
 * ============================================================
 */

static int mfrc522_spi_transfer(struct spi_device *spi,
                                const u8 *tx,
                                u8 *rx,
                                size_t len)
{
    struct spi_transfer xfer = {
        .tx_buf = tx,
        .rx_buf = rx,
        .len = len,
    };

    /*
     * spidev처럼 userspace SPI_IOC_MESSAGE를 거치는 것이 아니라
     * MFRC522 driver가 SPI Core를 직접 호출한다.
     */
    return spi_sync_transfer(spi, &xfer, 1);
}


/* ============================================================
 * MFRC522 Register Access
 * ============================================================
 */

static int mfrc522_read_reg(struct spi_device *spi,
                            u8 reg,
                            u8 *value)
{
    u8 tx[2];
    u8 rx[2] = { 0 };
    int ret;

    /*
     * MFRC522 SPI Read
     *
     * bit 7             : 1 = read
     * bit 6..1          : register address
     * bit 0             : 0
     */
    tx[0] = ((reg << 1) & 0x7E) | 0x80;
    tx[1] = 0x00;

    ret = mfrc522_spi_transfer(spi,
                               tx,
                               rx,
                               sizeof(tx));
    if (ret)
        return ret;

    *value = rx[1];

    return 0;
}


static int mfrc522_write_reg(struct spi_device *spi,
                             u8 reg,
                             u8 value)
{
    u8 tx[2];

    /*
     * MFRC522 SPI Write
     *
     * bit 7             : 0 = write
     * bit 6..1          : register address
     * bit 0             : 0
     */
    tx[0] = (reg << 1) & 0x7E;
    tx[1] = value;

    return mfrc522_spi_transfer(spi,
                                tx,
                                NULL,
                                sizeof(tx));
}


/* ============================================================
 * Register bit helper
 * ============================================================
 */

static int mfrc522_set_bits(struct spi_device *spi,
                            u8 reg,
                            u8 mask)
{
    u8 value;
    int ret;

    ret = mfrc522_read_reg(spi, reg, &value);
    if (ret)
        return ret;

    value |= mask;

    return mfrc522_write_reg(spi, reg, value);
}


static int mfrc522_clear_bits(struct spi_device *spi,
                              u8 reg,
                              u8 mask)
{
    u8 value;
    int ret;

    ret = mfrc522_read_reg(spi, reg, &value);
    if (ret)
        return ret;

    value &= ~mask;

    return mfrc522_write_reg(spi, reg, value);
}


/* ============================================================
 * MFRC522 Timer
 * ============================================================
 */

static int mfrc522_timer_init(struct spi_device *spi)
{
    u8 tmode;
    int ret;

    ret = mfrc522_write_reg(spi,
                            TModeReg,
                            TMODE_TAUTO);
    if (ret)
        return ret;

    ret = mfrc522_read_reg(spi,
                           TModeReg,
                           &tmode);
    if (ret)
        return ret;

    /*
     * TPrescaler = 0x2A6
     * 약 100 us / tick
     */
    tmode &= ~0x0F;
    tmode |= 0x02;

    ret = mfrc522_write_reg(spi,
                            TModeReg,
                            tmode);
    if (ret)
        return ret;

    return mfrc522_write_reg(spi,
                             TPrescalerReg,
                             0xA6);
}


static int mfrc522_timer_set(struct spi_device *spi,
                             u16 ticks)
{
    int ret;

    ret = mfrc522_write_reg(spi,
                            TReloadRegH,
                            (ticks >> 8) & 0xFF);
    if (ret)
        return ret;

    return mfrc522_write_reg(spi,
                             TReloadRegL,
                             ticks & 0xFF);
}


/* ============================================================
 * MFRC522 initialization / cleanup
 * ============================================================
 */

static int mfrc522_soft_reset(struct spi_device *spi)
{
    int ret;

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_SOFTRESET);
    if (ret)
        return ret;

    usleep_range(5000, 6000);

    return 0;
}


static int mfrc522_command_cleanup(struct spi_device *spi)
{
    int ret;

    ret = mfrc522_clear_bits(spi,
                             Status2Reg,
                             MFCRYPTO1ON);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_IDLE);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            FIFOLevelReg,
                            FIFO_FLUSH);
    if (ret)
        return ret;

    return mfrc522_write_reg(spi,
                             ComIrqReg,
                             0x7F);
}


static int mfrc522_hw_init(struct spi_device *spi)
{
    int ret;

    ret = mfrc522_soft_reset(spi);
    if (ret)
        return ret;

    ret = mfrc522_timer_init(spi);
    if (ret)
        return ret;

    /*
     * Antenna driver ON
     */
    ret = mfrc522_set_bits(spi,
                           TxControlReg,
                           0x03);
    if (ret)
        return ret;

    /*
     * ISO14443A 100% ASK
     */
    ret = mfrc522_set_bits(spi,
                           TxASKReg,
                           FORCE_100_ASK);
    if (ret)
        return ret;

    usleep_range(5000, 6000);

    return 0;
}


static void mfrc522_hw_cleanup(struct spi_device *spi)
{
    mfrc522_command_cleanup(spi);

    /*
     * Antenna OFF
     */
    mfrc522_clear_bits(spi,
                       TxControlReg,
                       0x03);
}


/* ============================================================
 * MFRC522 Transceive
 * ============================================================
 */

static int mfrc522_transceive(struct spi_device *spi,
                              const u8 *tx,
                              size_t tx_len,
                              u8 *rx,
                              size_t *rx_len)
{
    u8 irq;
    u8 fifo_level;
    size_t i;
    int ret;

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_IDLE);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            ComIrqReg,
                            0x7F);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            FIFOLevelReg,
                            FIFO_FLUSH);
    if (ret)
        return ret;

    /*
     * PICC로 보낼 데이터 FIFO 입력
     */
    for (i = 0; i < tx_len; i++) {
        ret = mfrc522_write_reg(spi,
                                FIFODataReg,
                                tx[i]);
        if (ret)
            return ret;
    }

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_TRANSCEIVE);
    if (ret)
        return ret;

    ret = mfrc522_set_bits(spi,
                           BitFramingReg,
                           START_SEND);
    if (ret)
        return ret;

    /*
     * MFRC522 timer interrupt 또는 RX 완료까지 대기
     */
    for (;;) {

        ret = mfrc522_read_reg(spi,
                               ComIrqReg,
                               &irq);
        if (ret)
            return ret;

        if (irq & COMIRQ_TIMER)
            return -ETIMEDOUT;

        if (irq & COMIRQ_RX)
            break;

        cpu_relax();
    }

    ret = mfrc522_read_reg(spi,
                           FIFOLevelReg,
                           &fifo_level);
    if (ret)
        return ret;

    if (fifo_level > *rx_len)
        return -EMSGSIZE;

    for (i = 0; i < fifo_level; i++) {

        ret = mfrc522_read_reg(spi,
                               FIFODataReg,
                               &rx[i]);
        if (ret)
            return ret;
    }

    *rx_len = fifo_level;

    return 0;
}


/* ============================================================
 * CRC_A
 * ============================================================
 */

static int mfrc522_calculate_crc(struct spi_device *spi,
                                 const u8 *data,
                                 size_t len,
                                 u8 crc[2])
{
    u8 irq;
    size_t i;
    int ret;

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_IDLE);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            ComIrqReg,
                            0x7F);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            FIFOLevelReg,
                            FIFO_FLUSH);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            DivIrqReg,
                            DIVIRQ_CRC);
    if (ret)
        return ret;

    for (i = 0; i < len; i++) {

        ret = mfrc522_write_reg(spi,
                                FIFODataReg,
                                data[i]);
        if (ret)
            return ret;
    }

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_CALCCRC);
    if (ret)
        return ret;

    /*
     * CRC calculation은 매우 짧지만
     * 비정상 상태에서 무한 loop가 되지 않도록 제한한다.
     */
    for (i = 0; i < 10000; i++) {

        ret = mfrc522_read_reg(spi,
                               DivIrqReg,
                               &irq);
        if (ret)
            return ret;

        if (irq & DIVIRQ_CRC)
            break;

        cpu_relax();
    }

    if (i == 10000)
        return -ETIMEDOUT;

    ret = mfrc522_read_reg(spi,
                           CRCResultRegL,
                           &crc[0]);
    if (ret)
        return ret;

    return mfrc522_read_reg(spi,
                            CRCResultRegH,
                            &crc[1]);
}


/* ============================================================
 * REQA
 * ============================================================
 */

static int mfrc522_request(struct spi_device *spi,
                           u8 atqa[2])
{
    u8 reqa = PICC_REQA;
    size_t rx_len = 2;
    int ret;

    ret = mfrc522_timer_set(spi,
                            REQA_TIMEOUT_TICKS);
    if (ret)
        return ret;

    /*
     * REQA는 7-bit short frame
     */
    ret = mfrc522_write_reg(spi,
                            BitFramingReg,
                            0x07);
    if (ret)
        return ret;

    ret = mfrc522_transceive(spi,
                             &reqa,
                             1,
                             atqa,
                             &rx_len);
    if (ret)
        return ret;

    if (rx_len != 2)
        return -EIO;

    return 0;
}


/* ============================================================
 * Anti-collision
 * ============================================================
 */

static int mfrc522_anticoll(struct spi_device *spi,
                            u8 sel,
                            u8 uid_part[5])
{
    u8 tx[2];
    size_t rx_len = 5;
    int ret;

    tx[0] = sel;
    tx[1] = PICC_ANTICOLL;

    ret = mfrc522_write_reg(spi,
                            BitFramingReg,
                            0x00);
    if (ret)
        return ret;

    ret = mfrc522_timer_set(spi,
                            ANTICOLL_TIMEOUT_TICKS);
    if (ret)
        return ret;

    ret = mfrc522_transceive(spi,
                             tx,
                             sizeof(tx),
                             uid_part,
                             &rx_len);
    if (ret)
        return ret;

    if (rx_len != 5)
        return -EIO;

    /*
     * BCC 검사
     */
    if ((uid_part[0] ^
         uid_part[1] ^
         uid_part[2] ^
         uid_part[3]) != uid_part[4])
        return -EBADMSG;

    return 0;
}


/* ============================================================
 * SELECT
 * ============================================================
 */

static int mfrc522_select(struct spi_device *spi,
                          u8 sel,
                          const u8 uid_part[5],
                          u8 *sak)
{
    u8 tx[9];
    u8 rx[3];
    u8 crc[2];

    size_t rx_len = sizeof(rx);
    int ret;

    tx[0] = sel;
    tx[1] = PICC_SELECT;

    tx[2] = uid_part[0];
    tx[3] = uid_part[1];
    tx[4] = uid_part[2];
    tx[5] = uid_part[3];
    tx[6] = uid_part[4];

    ret = mfrc522_calculate_crc(spi,
                                tx,
                                7,
                                crc);
    if (ret)
        return ret;

    tx[7] = crc[0];
    tx[8] = crc[1];

    ret = mfrc522_write_reg(spi,
                            BitFramingReg,
                            0x00);
    if (ret)
        return ret;

    ret = mfrc522_timer_set(spi,
                            SELECT_TIMEOUT_TICKS);
    if (ret)
        return ret;

    ret = mfrc522_transceive(spi,
                             tx,
                             sizeof(tx),
                             rx,
                             &rx_len);
    if (ret)
        return ret;

    if (rx_len != 3)
        return -EIO;

    ret = mfrc522_calculate_crc(spi,
                                rx,
                                1,
                                crc);
    if (ret)
        return ret;

    if (rx[1] != crc[0] ||
        rx[2] != crc[1])
        return -EBADMSG;

    *sak = rx[0];

    return 0;
}


/* ============================================================
 * UID Anti-collision + SELECT
 * ============================================================
 */

static int mfrc522_get_uid(struct spi_device *spi,
                           u8 uid[MFRC522_UID_MAX_LEN],
                           u8 *uid_len)
{
    u8 cl1[5];
    u8 cl2[5];
    u8 sak;
    int ret;

    ret = mfrc522_anticoll(spi,
                           PICC_SEL_CL1,
                           cl1);
    if (ret)
        return ret;

    /*
     * 4-byte UID
     */
    if (cl1[0] != PICC_CT) {

        uid[0] = cl1[0];
        uid[1] = cl1[1];
        uid[2] = cl1[2];
        uid[3] = cl1[3];

        ret = mfrc522_select(spi,
                             PICC_SEL_CL1,
                             cl1,
                             &sak);
        if (ret)
            return ret;

        if (sak & SAK_CASCADE_BIT)
            return -EIO;

        *uid_len = 4;

        return 0;
    }

    /*
     * 7-byte UID
     */
    uid[0] = cl1[1];
    uid[1] = cl1[2];
    uid[2] = cl1[3];

    ret = mfrc522_select(spi,
                         PICC_SEL_CL1,
                         cl1,
                         &sak);
    if (ret)
        return ret;

    if (!(sak & SAK_CASCADE_BIT))
        return -EIO;

    ret = mfrc522_anticoll(spi,
                           PICC_SEL_CL2,
                           cl2);
    if (ret)
        return ret;

    uid[3] = cl2[0];
    uid[4] = cl2[1];
    uid[5] = cl2[2];
    uid[6] = cl2[3];

    ret = mfrc522_select(spi,
                         PICC_SEL_CL2,
                         cl2,
                         &sak);
    if (ret)
        return ret;

    /*
     * 현재 기존 프로그램과 동일하게
     * 10-byte UID(CL3)는 지원하지 않는다.
     */
    if (sak & SAK_CASCADE_BIT)
        return -EOPNOTSUPP;

    *uid_len = 7;

    return 0;
}


/* ============================================================
 * MIFARE Authentication
 * ============================================================
 */

static int mfrc522_authenticate(struct spi_device *spi,
                                u8 block_addr,
                                const u8 key[6],
                                const u8 *uid,
                                u8 uid_len)
{
    u8 auth[12];
    u8 irq;
    u8 status2;

    int i;
    int ret;

    if (uid_len != 4 && uid_len != 7)
        return -EINVAL;

    auth[0] = PICC_MF_AUTH_KEY_A;
    auth[1] = block_addr;

    for (i = 0; i < 6; i++)
        auth[2 + i] = key[i];

    /*
     * UID 마지막 4 bytes 사용
     */
    auth[8]  = uid[uid_len - 4];
    auth[9]  = uid[uid_len - 3];
    auth[10] = uid[uid_len - 2];
    auth[11] = uid[uid_len - 1];

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_IDLE);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            FIFOLevelReg,
                            FIFO_FLUSH);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            ComIrqReg,
                            0x7F);
    if (ret)
        return ret;

    for (i = 0; i < 12; i++) {

        ret = mfrc522_write_reg(spi,
                                FIFODataReg,
                                auth[i]);
        if (ret)
            return ret;
    }

    ret = mfrc522_timer_set(spi,
                            AUTH_TIMEOUT_TICKS);
    if (ret)
        return ret;

    ret = mfrc522_write_reg(spi,
                            CommandReg,
                            PCD_MFAUTHENT);
    if (ret)
        return ret;

    for (;;) {

        ret = mfrc522_read_reg(spi,
                               ComIrqReg,
                               &irq);
        if (ret)
            return ret;

        if (irq & COMIRQ_TIMER)
            return -ETIMEDOUT;

        if (irq & COMIRQ_IDLE)
            break;

        cpu_relax();
    }

    ret = mfrc522_read_reg(spi,
                           Status2Reg,
                           &status2);
    if (ret)
        return ret;

    if (!(status2 & MFCRYPTO1ON))
        return -EACCES;

    return 0;
}


/* ============================================================
 * MIFARE Block Read
 * ============================================================
 */

static int mfrc522_read_block(struct spi_device *spi,
                              u8 block_addr,
                              u8 data[MFRC522_BLOCK_SIZE])
{
    u8 tx[4];
    u8 rx[18];
    u8 crc[2];

    size_t rx_len = sizeof(rx);
    int i;
    int ret;

    tx[0] = PICC_READ;
    tx[1] = block_addr;

    ret = mfrc522_calculate_crc(spi,
                                tx,
                                2,
                                crc);
    if (ret)
        return ret;

    tx[2] = crc[0];
    tx[3] = crc[1];

    ret = mfrc522_write_reg(spi,
                            BitFramingReg,
                            0x00);
    if (ret)
        return ret;

    ret = mfrc522_timer_set(spi,
                            READ_TIMEOUT_TICKS);
    if (ret)
        return ret;

    ret = mfrc522_transceive(spi,
                             tx,
                             sizeof(tx),
                             rx,
                             &rx_len);
    if (ret)
        return ret;

    if (rx_len != 18)
        return -EIO;

    ret = mfrc522_calculate_crc(spi,
                                rx,
                                16,
                                crc);
    if (ret)
        return ret;

    if (rx[16] != crc[0] ||
        rx[17] != crc[1])
        return -EBADMSG;

    for (i = 0; i < MFRC522_BLOCK_SIZE; i++)
        data[i] = rx[i];

    /*
     * 기존 userspace 프로그램의 동작 유지.
     *
     * Sector Trailer의 Key A는 실제 READ로 읽을 수 없으므로
     * 기존 코드처럼 인증에 사용한 알려진 Key A 값을 채운다.
     */
    if ((block_addr % 4) == 3) {
        for (i = 0; i < 6; i++)
            data[i] = key_a[i];
    }

    return 0;
}


/* ============================================================
 * Common PICC selection
 * ============================================================
 */

static int mfrc522_select_card(struct spi_device *spi,
                               u8 uid[MFRC522_UID_MAX_LEN],
                               u8 *uid_len)
{
    u8 atqa[2];
    int ret;

    /*
     * 이전 command 상태 제거
     */
    ret = mfrc522_command_cleanup(spi);
    if (ret)
        return ret;

    ret = mfrc522_request(spi, atqa);
    if (ret)
        return ret;

    return mfrc522_get_uid(spi,
                           uid,
                           uid_len);
}


/* ============================================================
 * High-level operations
 * ============================================================
 */

static int mfrc522_op_read_uid(struct spi_device *spi,
                               struct mfrc522_uid *result)
{
    memset(result, 0, sizeof(*result));

    return mfrc522_select_card(spi,
                               result->uid,
                               &result->uid_len);
}


static int mfrc522_op_read_block(struct spi_device *spi,
                                 struct mfrc522_block *block)
{
    u8 uid[MFRC522_UID_MAX_LEN];
    u8 uid_len;
    u8 block_addr;

    int ret;

    if (block->sector > 15 ||
        block->block >= MFRC522_BLOCKS_PER_SECTOR)
        return -EINVAL;

    ret = mfrc522_select_card(spi,
                              uid,
                              &uid_len);
    if (ret)
        return ret;

    block_addr =
        block->sector * MFRC522_BLOCKS_PER_SECTOR +
        block->block;

    ret = mfrc522_authenticate(spi,
                               block_addr,
                               key_a,
                               uid,
                               uid_len);
    if (ret)
        return ret;

    return mfrc522_read_block(spi,
                              block_addr,
                              block->data);
}


static int mfrc522_op_read_sector(struct spi_device *spi,
                                  struct mfrc522_sector *sector)
{
    u8 uid[MFRC522_UID_MAX_LEN];
    u8 uid_len;
    u8 sector_addr;

    int i;
    int ret;

    if (sector->sector > 15)
        return -EINVAL;

    ret = mfrc522_select_card(spi,
                              uid,
                              &uid_len);
    if (ret)
        return ret;

    sector_addr =
        sector->sector * MFRC522_BLOCKS_PER_SECTOR;

    /*
     * 기존 readSector 프로그램과 동일하게
     * sector 시작 block에 대해 Authentication 1회
     */
    ret = mfrc522_authenticate(spi,
                               sector_addr,
                               key_a,
                               uid,
                               uid_len);
    if (ret)
        return ret;

    for (i = 0;
         i < MFRC522_BLOCKS_PER_SECTOR;
         i++) {

        ret = mfrc522_read_block(spi,
                                 sector_addr + i,
                                 sector->data[i]);
        if (ret)
            return ret;
    }

    return 0;
}


/* ============================================================
 * ioctl
 * ============================================================
 */

static long mfrc522_ioctl(struct file *filp,
                          unsigned int cmd,
                          unsigned long arg)
{
    struct mfrc522_data *mfrc522;
    struct spi_device *spi;

    int ret = 0;

    if (_IOC_TYPE(cmd) != MFRC522_IOC_MAGIC)
        return -ENOTTY;

    mfrc522 = filp->private_data;

    if (!mfrc522)
        return -ENODEV;

    /*
     * remove()와 실제 SPI operation의 동시 실행 방지.
     *
     * 또한 한 MFRC522에서 두 userspace command가 동시에
     * RFID command/FIFO를 변경하는 것도 방지한다.
     */
    mutex_lock(&mfrc522->spi_lock);

    spi = mfrc522->spi;

    if (!spi) {
        ret = -ESHUTDOWN;
        goto out_unlock;
    }

    switch (cmd) {

    case MFRC522_IOC_READ_UID:
    {
        struct mfrc522_uid uid;

        ret = mfrc522_op_read_uid(spi, &uid);
        if (ret)
            break;

        if (copy_to_user((void __user *)arg,
                         &uid,
                         sizeof(uid)))
            ret = -EFAULT;

        break;
    }


    case MFRC522_IOC_READ_BLOCK:
    {
        struct mfrc522_block block;

        if (copy_from_user(&block,
                           (void __user *)arg,
                           sizeof(block))) {
            ret = -EFAULT;
            break;
        }

        ret = mfrc522_op_read_block(spi,
                                    &block);
        if (ret)
            break;

        if (copy_to_user((void __user *)arg,
                         &block,
                         sizeof(block)))
            ret = -EFAULT;

        break;
    }


    case MFRC522_IOC_READ_SECTOR:
    {
        struct mfrc522_sector sector;

        if (copy_from_user(&sector,
                           (void __user *)arg,
                           sizeof(sector))) {
            ret = -EFAULT;
            break;
        }

        ret = mfrc522_op_read_sector(spi,
                                     &sector);
        if (ret)
            break;

        if (copy_to_user((void __user *)arg,
                         &sector,
                         sizeof(sector)))
            ret = -EFAULT;

        break;
    }


    default:
        ret = -ENOTTY;
        break;
    }


out_unlock:

    mutex_unlock(&mfrc522->spi_lock);

    return ret;
}


/* ============================================================
 * open / release
 *
 * spidev_open / spidev_release와 같은 방식
 * ============================================================
 */

static int mfrc522_open(struct inode *inode,
                        struct file *filp)
{
    struct mfrc522_data *mfrc522 = NULL;
    struct mfrc522_data *iter;

    int ret = -ENXIO;

    mutex_lock(&mfrc522_device_list_lock);

    /*
     * inode의 dev_t와 동일한 MFRC522 device 찾기
     */
    list_for_each_entry(iter,
                        &mfrc522_device_list,
                        device_entry) {

        if (iter->devt == inode->i_rdev) {
            mfrc522 = iter;
            break;
        }
    }

    if (!mfrc522)
        goto out;

    /*
     * 첫 open에서만 MFRC522 초기화
     */
    if (mfrc522->users == 0) {

        mutex_lock(&mfrc522->spi_lock);

        if (!mfrc522->spi) {
            ret = -ESHUTDOWN;
            mutex_unlock(&mfrc522->spi_lock);
            goto out;
        }

        ret = mfrc522_hw_init(mfrc522->spi);

        mutex_unlock(&mfrc522->spi_lock);

        if (ret)
            goto out;
    }

    mfrc522->users++;

    filp->private_data = mfrc522;

    stream_open(inode, filp);

    ret = 0;


out:

    mutex_unlock(&mfrc522_device_list_lock);

    return ret;
}


static int mfrc522_release(struct inode *inode,
                           struct file *filp)
{
    struct mfrc522_data *mfrc522;
    bool dofree = false;

    mutex_lock(&mfrc522_device_list_lock);

    mfrc522 = filp->private_data;
    filp->private_data = NULL;

    if (!mfrc522)
        goto out;

    mutex_lock(&mfrc522->spi_lock);

    /*
     * 마지막 close라면 hardware cleanup
     */
    if (mfrc522->users == 1 &&
        mfrc522->spi)
        mfrc522_hw_cleanup(mfrc522->spi);

    /*
     * remove()가 이미 실행되었으면
     * spi == NULL.
     *
     * 마지막 user가 close할 때 memory free.
     */
    dofree = (mfrc522->spi == NULL);

    mutex_unlock(&mfrc522->spi_lock);

    if (mfrc522->users > 0)
        mfrc522->users--;

    if (mfrc522->users == 0 &&
        dofree)
        kfree(mfrc522);


out:

    mutex_unlock(&mfrc522_device_list_lock);

    return 0;
}


/* ============================================================
 * file_operations
 * ============================================================
 */

static const struct file_operations mfrc522_fops = {
    .owner          = THIS_MODULE,

    .unlocked_ioctl = mfrc522_ioctl,

    .open           = mfrc522_open,
    .release        = mfrc522_release,
};


/* ============================================================
 * SPI probe
 * ============================================================
 */

static int mfrc522_probe(struct spi_device *spi)
{
    struct mfrc522_data *mfrc522;
    struct device *dev;

    unsigned long minor;

    int ret;

    dev_info(&spi->dev,
             "MFRC522 probe called\n");


    mfrc522 = kzalloc(sizeof(*mfrc522),
                      GFP_KERNEL);

    if (!mfrc522)
        return -ENOMEM;


    mfrc522->spi = spi;

    mutex_init(&mfrc522->spi_lock);

    INIT_LIST_HEAD(&mfrc522->device_entry);


    /*
     * SPI speed는 Device Tree의
     *
     * spi-max-frequency = <1000000>;
     *
     * 값을 사용한다.
     */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;

    ret = spi_setup(spi);
    if (ret) {
        dev_err(&spi->dev,
                "spi_setup failed: %d\n",
                ret);

        goto err_free;
    }


    dev_info(&spi->dev,
             "SPI configured: mode=%u bits=%u speed=%u Hz\n",
             spi->mode,
             spi->bits_per_word,
             spi->max_speed_hz);

    /*
     * spidev처럼 사용 가능한 minor 확보
     */
    mutex_lock(&mfrc522_device_list_lock);


    minor = find_first_zero_bit(minors,
                                N_MFRC522_MINORS);

    if (minor >= N_MFRC522_MINORS) {

        ret = -ENODEV;

        goto err_unlock;
    }


    mfrc522->devt =
        MKDEV(mfrc522_major, minor);


    dev = device_create(&mfrc522_class,
                        &spi->dev,
                        mfrc522->devt,
                        mfrc522,
                        "mfrc522%lu",
                        minor);

    if (IS_ERR(dev)) {

        ret = PTR_ERR(dev);

        goto err_unlock;
    }


    set_bit(minor, minors);


    list_add(&mfrc522->device_entry,
             &mfrc522_device_list);


    mutex_unlock(&mfrc522_device_list_lock);


    /*
     * struct spi_device
     *        ↕
     * struct mfrc522_data
     */
    spi_set_drvdata(spi, mfrc522);


    dev_info(&spi->dev,
             "/dev/mfrc522%lu registered\n",
             minor);


    return 0;


err_unlock:

    mutex_unlock(&mfrc522_device_list_lock);


err_free:

    kfree(mfrc522);

    return ret;
}


/* ============================================================
 * SPI remove
 *
 * spidev_remove와 같은 lifetime 처리
 * ============================================================
 */

static void mfrc522_remove(struct spi_device *spi)
{
    struct mfrc522_data *mfrc522;

    mfrc522 = spi_get_drvdata(spi);


    /*
     * 새로운 open 방지
     */
    mutex_lock(&mfrc522_device_list_lock);


    /*
     * 이미 열려 있는 fd가 ioctl 수행 시
     * -ESHUTDOWN을 반환하도록 한다.
     */
    mutex_lock(&mfrc522->spi_lock);

    mfrc522->spi = NULL;

    mutex_unlock(&mfrc522->spi_lock);


    list_del(&mfrc522->device_entry);


    device_destroy(&mfrc522_class,
                   mfrc522->devt);


    clear_bit(MINOR(mfrc522->devt),
              minors);


    /*
     * open된 fd가 없으면 즉시 free.
     *
     * 있다면 마지막 release()에서 free.
     */
    if (mfrc522->users == 0)
        kfree(mfrc522);


    mutex_unlock(&mfrc522_device_list_lock);


    dev_info(&spi->dev,
             "MFRC522 removed\n");
}


/* ============================================================
 * Device Tree matching
 * ============================================================
 */

static const struct of_device_id mfrc522_of_match[] = {
    {
        .compatible = "seminar,mfrc522"
    },
    { }
};

MODULE_DEVICE_TABLE(of, mfrc522_of_match);


/*
 * SPI modalias / module autoload용 ID
 */
static const struct spi_device_id mfrc522_spi_ids[] = {
    { "mfrc522", 0 },
    { }
};

MODULE_DEVICE_TABLE(spi, mfrc522_spi_ids);


/* ============================================================
 * SPI Driver
 * ============================================================
 */

static struct spi_driver mfrc522_spi_driver = {

    .driver = {
        .name = "mfrc522",
        .of_match_table =mfrc522_of_match,
    },

    .probe = mfrc522_probe,
    .remove = mfrc522_remove,
    .id_table = mfrc522_spi_ids,
};


/* ============================================================
 * Module init
 *
 * spidev와 동일한 큰 흐름
 *
 * register_chrdev
 *       ↓
 * class_register
 *       ↓
 * spi_register_driver
 * ============================================================
 */

static int __init mfrc522_driver_init(void)
{
    int ret;


    /*
     * major = 0
     * → dynamic major 할당
     */
    mfrc522_major =
        register_chrdev(0,
                        MFRC522_NAME,
                        &mfrc522_fops);

    if (mfrc522_major < 0)
        return mfrc522_major;


    ret = class_register(&mfrc522_class);

    if (ret) {

        unregister_chrdev(mfrc522_major,
                          MFRC522_NAME);

        return ret;
    }


    /*
     * 마지막에 SPI driver 등록.
     *
     * 등록 직후 기존 spi_device와 match되면
     * mfrc522_probe()가 바로 호출될 수 있다.
     */
    ret = spi_register_driver(&mfrc522_spi_driver);

    if (ret) {

        class_unregister(&mfrc522_class);

        unregister_chrdev(mfrc522_major,
                          MFRC522_NAME);

        return ret;
    }


    pr_info("mfrc522: driver registered, major=%d\n",
            mfrc522_major);


    return 0;
}


static void __exit mfrc522_driver_exit(void)
{
    /*
     * 등록 역순으로 제거
     */
    spi_unregister_driver(&mfrc522_spi_driver);

    class_unregister(&mfrc522_class);

    unregister_chrdev(mfrc522_major,
                      MFRC522_NAME);


    pr_info("mfrc522: driver unloaded\n");
}


module_init(mfrc522_driver_init);
module_exit(mfrc522_driver_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lee Junghyun");
MODULE_DESCRIPTION("MFRC522 dedicated SPI device driver");