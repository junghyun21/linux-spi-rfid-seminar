#include "spi_dev.h"
#include <fcntl.h>
#include <sys/ioctl.h>

int spi_fd;

// Application -> open() -> /dev/spidev0.0
int spi_init(void) 
{
    uint8_t mode = MODE;
    uint8_t bits = BIT_LEN;
    uint32_t speed = SPEED_HZ;   // 일단 1 Mbit/s (SPI up to 10 Mbit/s)

    spi_fd = open(SPI_DEVICE, O_RDWR);

    if(spi_fd < 0) {
        perror("open");
        return -1;
    }

    // printf("[DEBUG] SPI_DEVICE = %s, spi_fd = %d\n",
    //        SPI_DEVICE, spi_fd);

    if (ioctl(spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI_IOC_WR_MODE");
        return -1;
    }

    if (ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0) {
        perror("SPI_IOC_WR_BITS_PER_WORD");
        return -1;
    }

    if(ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI_IOC_WR_MAX_SPEED_HZ");
        return -1;
    }

    return 0;
}

// userspace -> spidev -> SPI core -> spi_bcm2835 -> BCM2711 SPI Controller -> MFRC522
int spi_transfer(uint8_t *tx,
                        uint8_t *rx,
                        size_t len)
{
    struct spi_ioc_transfer tr = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = len,
        .speed_hz = SPEED_HZ,
        .bits_per_word = BIT_LEN,
    };

    int ret = ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);

    if (ret < 0) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }

    return 0;
}