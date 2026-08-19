# MFRC522 Control using Linux `spidev`

Linux의 범용 SPI Device Driver인 `spidev`를 이용해 Raspberry Pi 4B에서 MFRC522를 제어하는 userspace C 프로그램입니다.

이 프로그램은 다음 두 가지 방식으로 사용할 수 있습니다.

1. `rfidctl`을 실행하여 대화형 터미널처럼 사용
2. `readUID`, `readBlock`, `readSector` 실행파일을 각각 직접 실행

| 프로그램 | 기능 |
|---|---|
| `rfidctl` | UID / Block / Sector 읽기 기능을 하나의 대화형 CLI에서 실행 |
| `readUID` | PICC의 UID 읽기 |
| `readBlock` | MIFARE Classic의 특정 Sector/Block 읽기 |
| `readSector` | MIFARE Classic의 특정 Sector 전체 읽기 |

자세한 사용법은 [linux-spi-rfid-seminar > README.md > 6. 프로그램 사용법](../README.md) 참고

<br>

## 목차

1. [동작 구조](#1-동작-구조)  
2. [환경](#2-환경)  
3. [프로젝트 파일 구성](#3-프로젝트-파일-구성)  
4. [빌드](#4-빌드)  
5. [빠른 시작](#5-빠른-시작)  

<br>

## 1. 동작 구조

```text
readUID / readBlock / readSector
            ↓
        MFRC522.c
            ↓
        spi_dev.c
            ↓
 ioctl(SPI_IOC_MESSAGE)
            ↓
      /dev/spidev0.0
            ↓
          spidev
            ↓
         SPI Core
            ↓
      spi_bcm2835
            ↓
         MFRC522
```

`spidev`는 MFRC522의 Register나 RFID Protocol을 알지 못합니다. 따라서 REQA, Anti-collision, SELECT, Authentication, READ, CRC 처리는 모두 userspace에서 수행합니다.

<br>

## 2. 환경

```text
Build Host : Windows 11 + WSL Ubuntu
Compiler   : aarch64-linux-gnu-gcc
Target     : AArch64 Linux

SPI Device : /dev/spidev0.0
SPI Mode   : Mode 0
Bits       : 8 bit
SPI Speed  : 1 MHz
```

프로그램 실행 전 Raspberry Pi에서 SPI가 활성화되어 있고 `/dev/spidev0.0` 장치 파일이 존재해야 합니다.

확인:

```bash
ls -l /dev/spidev*
```

출력 예시:

```text
crw-rw---- 1 root spi 153, 0 Jun 20 02:37 /dev/spidev0.0
```

장치 파일이 없다면 먼저 Raspberry Pi의 SPI 설정(config.txt)과 Device Tree 구성을 확인합니다.


현재 [`include/spi_dev.h`](./include/spi_dev.h) 설정:

```c
#define SPI_DEVICE  "/dev/spidev0.0"
#define BIT_LEN     8U
#define SPEED_HZ    1000000U
#define MODE        SPI_MODE_0
```

사용하는 SPI controller, Chip Select 또는 Device Tree 설정이 다르면 환경에 맞게 수정합니다.

<br>

## 3. 프로젝트 파일 구성

```text
spidev/
├── Makefile
├── README.md
├── bin/
│   ├── readBlock
│   ├── readSector
│   └── readUID
├── include/
│   ├── MFRC522.h
│   ├── PICC.h
│   └── spi_dev.h
├── rfidctl
└── src/
    ├── MFRC522.c
    ├── readBlock.c
    ├── readSector.c
    ├── readUID.c
    ├── rfidctl.c
    └── spi_dev.c
```

```text
src/      : C source
include/  : Header
bin/      : readUID / readBlock / readSector 실행파일
rfidctl   : 프로젝트 root에서 실행하는 대화형 CLI
```

`rfidctl`은 `bin/`의 개별 실행파일을 `fork()` + `execv()`로 실행합니다.

- `MFRC522.c`: MFRC522 Register 접근 및 RFID Protocol 처리
- `spi_dev.c`: `/dev/spidev0.0` open, SPI 설정, `SPI_IOC_MESSAGE`
- `readUID.c`: UID 읽기
- `readBlock.c`: 특정 Block 읽기
- `readSector.c`: 특정 Sector 읽기
- `rfidctl.c`: 대화형 CLI

<br>

## 4. 빌드

프로젝트 디렉터리로 이동합니다.

```bash
cd spidev
```

전체 프로그램 빌드:

```bash
make
```

빌드가 정상적으로 완료되면 다음 실행파일이 생성됩니다.

```text
./rfidctl
./bin/readUID
./bin/readBlock
./bin/readSector
```

생성 여부 확인:

```bash
ls -l rfidctl bin/
```

빌드 결과 삭제:

```bash
make clean
```

<br>

## 5. 빠른 시작

```bash
# 1. SPI 장치 확인 - Raspberry Pi
ls -l /dev/spidev*

# 2. 전체 빌드 (rfidctl과 .bin/ 내 실행파일이 없을 시)
make

# 3. 대화형 CLI 실행
./rfidctl

# 4. UID 단독 테스트
.bin/readUID

# 5. Block 단독 테스트
.bin/readBlock 0 0

# 6. Sector 단독 테스트
.bin/readSector 0
```