#ifndef SPI_DEV_H
#define SPI_DEV_H

#include <linux/spi/spidev.h>
#include <stdio.h>
#include <stdint.h>

#define SPI_DEVICE  "/dev/spidev0.0"
#define BIT_LEN     8U
#define SPEED_HZ    1000000U            // 일단 1 Mbit/s (SPI up to 10 Mbit/s)
#define MODE        SPI_MODE_0

int spi_init(void);
int spi_transfer(uint8_t *tx, uint8_t *rx, size_t len);

extern int spi_fd;

#endif