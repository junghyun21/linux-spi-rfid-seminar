# Linux SPI RFID Seminar

Raspberry Pi 4B와 MFRC522 RFID 모듈을 이용해 **RFID 모듈과 SPI 통신하고, MIFARE Classic 카드의 UID 및 메모리 데이터를 확인하는 C 프로그램**입니다.

해당 프로젝트는 MFRC522 RFID 모듈을 두 가지 방식으로 제어합니다.

1. [Linux 범용 SPI Device Driver인 `spidev` 사용](./spidev/)
2. [MFRC522 전용 Linux SPI Device Driver 직접 구현](./mfrc522_driver/) 

즉, 제공하는 **기능은 동일**하지만, 해당 기능을 수행하기 위해 거치는 **Linux 내부 제어 경로와 장치 제어 로직의 위치가 다릅니다.**

<br>

## 목차

1. [사용 장치 및 환경](#1-사용-장치-및-환경)  
    1.1 [Hardware](#11-hardware)  
    1.2 [Build Environment](#12-build-environment)  
2. [spidev(범용 SPI 드라이버) 방식](#2-spidev범용-spi-드라이버-방식)  
3. [MFRC522 전용 Driver 방식](#3-mfrc522-전용-driver-방식)  
4. [두 방식 비교](#4-두-방식-비교)  
5. [프로젝트 구조](#5-프로젝트-구조)
6. [프로그램 사용법](#6-프로그램-사용법)  
    4.1 [내장 명령어 요약](#61-내장-명령어-요약)  
    4.2 [UID 읽기](#62-uid-읽기)  
    4.3 [Block 읽기](#63-block-읽기)  
    4.4 [Sector 읽기](#64-sector-읽기)  
    4.5 [도움말](#65-도움말)  
    4.6 [종료](#66-종료)  
7. [MIFARE Classic 1K 메모리 구조](#7-mifare-classic-1k-메모리-구조)  
    5.1 [Sector Trailer 주의사항](#71-sector-trailer-주의사항)  
    5.2 [5.2 기본 인증 Key (Key A)](#72-기본-인증-key-key-a)  
8. [RFID 처리 흐름](#8-rfid-처리-흐름)  

<br>

## 1. 사용 장치 및 환경

```text
Board       : Raspberry Pi 4 Model B
RFID Reader : MFRC522
Card        : MIFARE Classic EV1 1K
Build Host  : Windows 11 + WSL Ubuntu
Compiler    : aarch64-linux-gnu-gcc
Target      : AArch64 Linux
```

### 1.1 Hardware

- **Raspberry Pi 4 Model B**
  - https://www.raspberrypi.com/products/raspberry-pi-4-model-b/
- **MFRC522 기반 RFID 모듈**
  - RC522 RFID 모듈 제품: https://handsontec.com/index.php/product/rc522-rfid-reader-module/
  - NXP MFRC522:
    https://www.nxp.com/products/rfid-nfc/nfc-hf/nfc-readers/standard-performance-mifare-and-ntag-frontend:MFRC522 
- **MIFARE Classic EV1 1K 카드**
  - NXP MIFARE Classic EV1:
    https://www.nxp.com/docs/en/data-sheet/MF1S50YYX_V1.pdf

### 1.2 Build Environment

현재 프로젝트는 **Windows 11의 WSL Ubuntu 환경에서 Raspberry Pi용 실행파일을 Cross Compile**하는 구성을 기준으로 합니다.

```text
Build Host : Windows 11 + WSL Ubuntu
Compiler   : aarch64-linux-gnu-gcc
Target     : AArch64 Linux
```

흐름:

```text
WSL Ubuntu
   │
   │ aarch64-linux-gnu-gcc
   ▼
AArch64 실행파일 생성
   │
   │ 네트워크(scp, ...)로 전달
   ▼
Raspberry Pi 4B에서 실행
```

<br>

## 2. spidev(범용 SPI 드라이버) 방식

```text
Application
   ↓
/dev/spidev0.0
   ↓
spidev
   ↓
SPI Core
   ↓
spi_bcm2835
   ↓
BCM2711 SPI Controller
   ↓
MFRC522
```

MFRC522 Register 접근, REQA, Anti-collision, SELECT, Authentication, READ, CRC 처리는 userspace의 [`MFRC522.c`](./spidev/src/MFRC522.c)에서 수행합니다.

자세한 내용: [`spidev/README.md`](./spidev/README.md)

<br>

## 3. MFRC522 전용 Driver 방식

```text
Application
   ↓
/dev/mfrc5220
   ↓
mfrc522_drv
   ↓
SPI Core
   ↓
spi_bcm2835
   ↓
BCM2711 SPI Controller
   ↓
MFRC522
```

Application은 `READ_UID`, `READ_BLOCK`, `READ_SECTOR`와 같은 의미 단위의 ioctl만 요청하고, 실제 RFID Protocol과 Register 접근은 [Kernel Driver](./mfrc522_driver/kernel/mfrc522_drv.c)가 수행합니다.

자세한 내용: [`mfrc522_driver/README.md`](./mfrc522_driver/README.md)

<br>

## 4. 두 방식 비교


핵심 차이는 **장치 고유 로직이 어느 계층에 위치하는가**입니다.

| 항목 | `spidev` | MFRC522 전용 Driver |
|---|---|---|
| 장치 파일 | `/dev/spidev0.0` | `/dev/mfrc5220` |
| Device Driver | Linux `spidev` | 직접 구현한 `mfrc522_drv` |
| SPI 설정 | userspace ioctl | Driver `probe()` |
| Register 접근 | userspace | kernel |
| RFID Protocol 처리 | userspace | kernel |
| SPI 전달 | `SPI_IOC_MESSAGE` | `spi_sync_transfer()` |
| Application 역할 | 장치 제어 로직 포함 | 기능 요청 중심 |

<br>

## 5. 프로젝트 구조

```text
linux-spi-rfid-seminar/
├── README.md
├── mfrc522_driver
│   ├── Makefile
│   ├── README.md
│   ├── bin
│   ├── include
│   ├── kernel
│   ├── rfidctl_mfrc522_driver  # 실행 프로그램
│   └── src
└── spidev
    ├── Makefile
    ├── README.md
    ├── bin
    ├── include
    ├── rfidctl                 # 실행 프로그램
    └── src
```

<br>

## 6. 프로그램 사용법

프로그램은 각 프로젝트 디렉토리([./spidev](./spidev/), [./mfrc522_driver](./mfrc522_driver/))의 root에 생성됩니다. 따라서 프로젝트 root에서 다음과 같이 실행합니다.

실행:

```bash
./rfidctl 또는 ./rfidctl_mfrc522_driver
```

실행 화면:

```text
RFID Control Terminal
Type 'help' for commands.
Type 'exit' or 'quit' to terminate.

rfidctl>
```

### 6.1 내장 명령어 요약

| 명령 | 설명 |
|---|---|
| `uid` | 카드의 UID를 읽습니다. (4byte or 7byte) |
| `readblock <sector> <block>` | 지정한 Sector의 Block 하나를 읽습니다. |
| `readsector <sector>` | 지정한 Sector의 Block 4개를 순서대로 읽습니다. |
| `help` | 사용할 수 있는 명령과 예제를 출력합니다. |
| `exit`, `quit` | 프로그램(rfidctl)을 종료합니다. |

cf. `rfidctl`을 사용하지 않고 `bin/`에 생성된 실행파일을 직접 사용할 수도 있습니다.

### 6.2 UID 읽기

```text
uid
```

출력 예시:

```text
rfidctl> uid
UID = 93 AC 51 56
```

### 6.3 Block 읽기

```text
readblock <sector> <block>
```

- sector : 0 ~ 15
- block  : 0 ~ 3

출력 예시:

```text
rfidctl> readblock 0 0
Block Data = 93 AC 51 56 38 08 04 00 62 63 64 65 66 67 68 69
```

### 6.4 Sector 읽기

```
readsector <sector>
```

- sector : 0 ~ 15


출력 예시:

```text
rfidctl> readsector 0
93 AC 51 56 38 08 04 00 62 63 64 65 66 67 68 69
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
FF FF FF FF FF FF FF 07 80 69 FF FF FF FF FF FF  <- Sector Trailer
```

### 6.5 도움말

```text
help
```

출력 예시:

```text
Commands:
  uid                           Read UID
  readblock <sector> <block>    Read block
  readsector <sector>           Read sector
  help                          Show help
  exit                          Exit rfidctl

Examples:
  uid
  readblock 0 0
  readsector 0
```

### 6.6 종료

```
exit | quit
```

종료하면 `rfidctl` 프로세스가 끝나고 shell prompt로 돌아갑니다.

<br>

## 7. MIFARE Classic 1K 메모리 구조

현재 프로그램의 Sector/Block 입력은 MIFARE Classic 1K의 16 Sector × 4 Block 구조를 기준으로 합니다.

```text
Sector 0
 ├─ Block 0
 ├─ Block 1
 ├─ Block 2
 └─ Block 3  <- Sector Trailer

Sector 1
 ├─ Block 0
 ├─ Block 1
 ├─ Block 2
 └─ Block 3  <- Sector Trailer

...

Sector 15
 ├─ Block 0
 ├─ Block 1
 ├─ Block 2
 └─ Block 3  <- Sector Trailer
```

### 7.1 Sector Trailer 주의사항

각 Sector의 마지막 Block은 일반 데이터 Block이 아니라 **Sector Trailer**입니다.

```text
Byte 0 ~ 5   : Key A
Byte 6 ~ 8   : Access Bits
Byte 9       : GPB
Byte 10 ~ 15 : Key B
```

Sector Trailer를 READ했을 때 Key A 영역은 일반 READ 응답으로 직접 얻을 수 없으므로 실제 카드 응답에서는 다음처럼 `00`으로 보일 수 있습니다.

```text
00 00 00 00 00 00 FF 07 80 69 FF FF FF FF FF FF
```

본 프로그램은 Authentication에 사용한 Key A를 이미 알고 있으므로, Sector Trailer 결과를 표시할 때 **프로그램이 실제 Authentication에 사용한 Key A 값을 해당 위치에 출력하도록 구성**했습니다.

스마트폰 Dump Editor 역시 인증에 성공한 Key 또는 이미 알고 있는 Key 정보를 화면에 표시할 수 있으므로 raw READ 응답과 화면에 표시되는 dump 값은 구분해서 봐야 합니다.

### 7.2 기본 인증 Key (Key A)

현재 공통 MFRC522 코드에는 기본 Key A가 다음과 같이 정의되어 있습니다.

```text
FF FF FF FF FF FF
```

다른 Key를 사용하는 Sector를 읽으려면 `key_a` 값을 해당 카드 설정에 맞게 변경해야 합니다.

<br>

## 8. RFID 처리 흐름

Block READ 기준:

```text
REQA
  ↓
ATQA
  ↓
Anti-collision
  ↓
SELECT
  ↓
UID 획득
  ↓
MIFARE Authentication
  ↓
READ
  ↓
16-byte Block Data
```

Sector READ는 한 Sector에 대해 Authentication한 후 해당 Sector의 Block 4개를 순서대로 READ합니다.