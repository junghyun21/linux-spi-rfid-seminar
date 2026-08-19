# MFRC522 Control using Dedicated Linux Device Driver

직접 구현한 MFRC522 전용 Linux SPI Device Driver를 이용해 Raspberry Pi 4B에서 MFRC522를 제어하는 userspace C 프로그램입니다.

이 프로그램은 다음 두 가지 방식으로 사용할 수 있습니다.

1. `rfidctl_mfrc522_driver`를 실행하여 대화형 터미널처럼 사용
2. `readUID`, `readBlock`, `readSector` 실행파일을 각각 직접 실행

| 프로그램 | 기능 |
|---|---|
| `rfidctl_mfrc522_driver` | UID / Block / Sector 읽기 기능을 하나의 대화형 CLI에서 실행 |
| `readUID` | PICC의 UID 읽기 |
| `readBlock` | MIFARE Classic의 특정 Sector/Block 읽기 |
| `readSector` | MIFARE Classic의 특정 Sector 전체 읽기 |

자세한 사용법은 [linux-spi-rfid-seminar > README.md > 6. 프로그램 사용법](../README.md) 참고

<br>

## 목차

1. [동작 구조](#1-동작-구조)  
2. [환경](#2-환경)  
    2.1 [spi device 확인](#21-spi-device-확인)  
    2.2 [mfrc522 전용 장치 파일 확인](#22-mfrc522-전용-장치-파일-확인)  
    2.3 [device driver binding 확인](#23-device--driver-binding-확인)
3. [프로젝트 파일 구성](#3-프로젝트-파일-구성)  
4. [Device Tree Overlay](#4-device-tree-overlay)  
5. [Kernel Module](#5-kernel-module)  
6. [빌드](#6-빌드)  
    6.1 [kernel driver / device tree overlay 빌드](#61-kernel-driver--device-tree-overlay-빌드)  
    6.2 [userspace 프로그램 빌드](#62-userspace-프로그램-빌드)
7. [빠른 시작](#7-빠른-시작)  

<br>

## 1. 동작 구조

```text
readUID / readBlock / readSector
            ↓
   ioctl(MFRC522_IOC_*)
            ↓
      /dev/mfrc5220
            ↓
       mfrc522_drv
            ↓
    spi_sync_transfer()
            ↓
         SPI Core
            ↓
      spi_bcm2835
            ↓
         MFRC522
```

MFRC522 전용 Device Driver는 **MFRC522의 Register 접근과 RFID Protocol을 알고 있습니다.**

따라서 REQA, Anti-collision, SELECT, Authentication, READ, CRC 처리는 Kernel Driver 내부에서 수행하며, userspace 프로그램은 `ioctl()`을 통해 UID / Block / Sector 읽기와 같은 기능 단위의 요청만 전달합니다.

기존 `spidev` 방식과 제공하는 기능은 동일하지만, **MFRC522 제어 로직이 userspace가 아니라 Kernel Driver 내부에 위치**한다는 차이가 있습니다.

<br>

## 2. 환경

```text
Build Host : Windows 11 + WSL Ubuntu
Compiler   : aarch64-linux-gnu-gcc
Target     : AArch64 Linux

SPI Device : spi0.1
Device File: /dev/mfrc5220
SPI Mode   : Mode 0
Bits       : 8 bit
SPI Speed  : 1 MHz
```

전용 Driver를 사용하기 전에 Raspberry Pi에서 다음 상태가 준비되어 있어야 합니다.

- Device Tree Overlay가 적용되어 MFRC522가 `spi0.1` SPI Device로 등록되어 있어야 합니다.
- MFRC522 Kernel Module이 로드되어 `spi0.1`과 전용 Driver가 Binding되어 있어야 합니다.
- Binding 완료 후 `/dev/mfrc5220` 장치 파일이 생성되어 있어야 합니다.

정상적인 준비 상태는 다음과 같습니다.

```text
Device Tree Overlay
        ↓
     spi0.1 생성
        ↓
Kernel Module 로드
        ↓
Device / Driver Binding
        ↓
 /dev/mfrc5220 생성
```

### 2.1 SPI Device 확인

```bash
ls /sys/bus/spi/devices/
```

출력 예시:

```text
lrwxrwxrwx 1 root root 0 Aug 19 15:20 spi0.0 -> ../../../devices/platform/soc/fe204000.spi/spi_master/spi0/spi0.0
lrwxrwxrwx 1 root root 0 Aug 19 15:20 spi0.1 -> ../../../devices/platform/soc/fe204000.spi/spi_master/spi0/spi0.1
```

`spi0.1`이 존재하지 않는 경우 [4. Device Tree Overlay](#4-device-tree-overlay)를 참고합니다.

### 2.2 MFRC522 전용 장치 파일 확인

```bash
ls -l /dev/mfrc522*
```

출력 예시:

```text
crw------- 1 root root <major>, 0 ... /dev/mfrc5220
```

`/dev/mfrc5220`이 존재하지 않는 경우 [5. Kernel Module](#5-kernel-module)을 참고합니다.

### 2.3 Device / Driver Binding 확인

```bash
readlink -f /sys/bus/spi/devices/spi0.1/driver
```

출력 예시:

```text
/sys/bus/spi/drivers/mfrc522
```

`spi0.1`은 존재하지만 Driver 경로가 출력되지 않는 경우 [5. Kernel Module](#5-kernel-module)을 참고합니다.

<br>

## 3. 프로젝트 파일 구성

```text
mfrc522_driver/
├── Makefile
├── README.md
├── bin/
│   ├── readBlock
│   ├── readSector
│   └── readUID
├── include/
│   └── mfrc522_ioctl.h
├── kernel/
│   ├── Makefile
│   ├── mfrc522_drv.c
│   ├── mfrc522_ioctl.h
│   └── mfrc522_overlay.dts
├── rfidctl_mfrc522_driver
└── src/
    ├── readBlock.c
    ├── readSector.c
    ├── readUID.c
    └── rfidctl_mfrc522_driver.c
```

```text
src/                       : C source
include/                   : userspace ioctl Header
bin/                       : readUID / readBlock / readSector 실행파일
kernel/                    : MFRC522 전용 Kernel Driver 및 Device Tree Overlay
rfidctl_mfrc522_driver     : 프로젝트 root에서 실행하는 대화형 CLI
```

`rfidctl_mfrc522_driver`은 `bin/`의 개별 실행파일을 `fork()` + `execv()`로 실행합니다.

- `mfrc522_drv.c`: MFRC522 Register 접근, RFID Protocol 처리, ioctl 처리
- `mfrc522_overlay.dts`: SPI0 CE1에 MFRC522 Device 생성
- `mfrc522_ioctl.h`: userspace와 Kernel Driver 사이의 ioctl Interface 정의
- `readUID.c`: UID 읽기 요청
- `readBlock.c`: 특정 Block 읽기 요청
- `readSector.c`: 특정 Sector 읽기 요청
- `rfidctl_mfrc522_driver.c`: 대화형 CLI

<br>

## 4. Device Tree Overlay

기존 SPI0 CE1에는 `spidev1`이 연결되어 있습니다.

전용 MFRC522 Driver를 사용하기 위해 기존 `spidev1`을 비활성화하고, 동일한 Chip Select 1 위치에 MFRC522 Device를 새로 등록합니다. 

Device Tree Overlay : [`kernel/mfrc522_overlay.dts`](./kernel/mfrc522_overlay.dts)

Overlay 빌드:

```bash
cd mfrc522_driver/kernel
make overlay
```

생성 파일:

```text
mfrc522.dtbo
```

Raspberry Pi의 Overlay 디렉터리에 복사합니다.

환경에 따라 다음 중 실제 사용 중인 경로를 사용합니다.

```text
/boot/firmware/overlays/
/boot/overlays/
```

예:

```bash
sudo cp mfrc522.dtbo /boot/firmware/overlays/
```

이후 `config.txt`에 다음 항목을 추가합니다.

```text
dtparam=spi=on
dtoverlay=mfrc522
```

재부팅 후 SPI Device 생성 여부를 확인합니다.

```bash
ls -l /sys/bus/spi/devices/
```

출력 예시:

```text
spi0.0
spi0.1
```

MFRC522 Device의 `compatible` 확인:

```bash
tr -d '\0' < /sys/bus/spi/devices/spi0.1/of_node/compatible
echo
```

출력 예시:

```text
seminar,mfrc522
```

<br>

## 5. Kernel Module

[`kernel/mfrc522_drv.c`](./kernel/mfrc522_drv.c)는 MFRC522 전용 Linux SPI Device Driver입니다.

Driver는 Device Tree의 다음 값과 매칭됩니다.

```text
compatible = "seminar,mfrc522"
```

Device Tree Overlay로 `spi0.1` Device가 생성된 후 Kernel Module이 로드되면 SPI Bus에서 Device와 Driver Matching이 수행됩니다.

```text
Device Tree Overlay
        ↓
     spi0.1 생성
        ↓
compatible / OF Match
        ↓
  mfrc522_probe()
        ↓
 SPI Mode / Bits 설정
        ↓
Character Device 등록
        ↓
 /dev/mfrc5220 생성
```

Driver가 제공하는 주요 ioctl Interface:

```text
MFRC522_IOC_READ_UID
MFRC522_IOC_READ_BLOCK
MFRC522_IOC_READ_SECTOR
```

Kernel Module 빌드:

```bash
cd mfrc522_driver/kernel
make module
```

생성 파일:

```text
mfrc522_drv.ko
```

Raspberry Pi에서 Module을 로드합니다.

```bash
sudo insmod mfrc522_drv.ko
```

로드 상태 확인:

```bash
lsmod | grep mfrc522
```

Kernel Log 확인:

```bash
dmesg | tail -50
```

Device/Driver Binding 확인:

```bash
readlink -f /sys/bus/spi/devices/spi0.1/driver
```

장치 파일 확인:

```bash
ls -l /dev/mfrc522*
```

출력 예시:

```text
/dev/mfrc5220
```

`/dev/mfrc5220`의 `0`은 SPI Chip Select 번호가 아니라 전용 Driver에서 첫 번째 MFRC522 장치에 할당한 minor 번호입니다.

Module 제거:

```bash
sudo rmmod mfrc522_drv
```

<br>

## 6. 빌드

### 6.1 Kernel Driver / Device Tree Overlay 빌드

Kernel Driver 디렉터리로 이동합니다.

```bash
cd mfrc522_driver/kernel
```

Kernel Module 빌드:

```bash
make module
```

Device Tree Overlay 빌드:

```bash
make overlay
```

빌드 결과:

```text
mfrc522_drv.ko
mfrc522.dtbo
```

빌드 결과 삭제:

```bash
make clean
```

### 6.2 userspace 프로그램 빌드

프로젝트 디렉터리로 이동합니다.

```bash
cd mfrc522_driver
```

전체 프로그램 빌드:

```bash
make
```

빌드가 정상적으로 완료되면 다음 실행파일이 생성됩니다.

```text
./rfidctl_mfrc522_driver
./bin/readUID
./bin/readBlock
./bin/readSector
```

생성 여부 확인:

```bash
ls -l rfidctl_mfrc522_driver bin/
```

빌드 결과 삭제:

```bash
make clean
```

<br>

## 7. 빠른 시작

```bash
# 1. Kernel Module / Device Tree Overlay 빌드 - Linux Build Environment
cd mfrc522_driver/kernel
make module
make overlay

# 2. mfrc522.dtbo를 Raspberry Pi Overlay 디렉터리에 복사
sudo cp mfrc522.dtbo /boot/firmware/overlays/

# 3. config.txt에 Overlay 설정 추가
dtparam=spi=on
dtoverlay=mfrc522

# 4. Raspberry Pi 재부팅
sudo reboot

# 5. SPI Device 생성 확인 - Raspberry Pi
ls -l /sys/bus/spi/devices/

# 6. MFRC522 compatible 확인 - Raspberry Pi
tr -d '\0' < /sys/bus/spi/devices/spi0.1/of_node/compatible
echo

# 7. Kernel Module 로드 - Raspberry Pi
sudo insmod mfrc522_drv.ko

# 8. Device Driver Binding 확인 - Raspberry Pi
readlink -f /sys/bus/spi/devices/spi0.1/driver

# 9. 장치 파일 확인 - Raspberry Pi
ls -l /dev/mfrc522*

# 10. userspace 전체 빌드 - Linux Build Environment
cd ..
make

# 11. 대화형 CLI 실행 - Raspberry Pi
./rfidctl_mfrc522_driver

# 12. UID 단독 테스트
./bin/readUID

# 13. Block 단독 테스트
./bin/readBlock 0 0

# 14. Sector 단독 테스트
./bin/readSector 0
```