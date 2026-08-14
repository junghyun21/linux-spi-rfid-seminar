# MFRC522 RFID Control Tool

Raspberry Pi에서 Linux `spidev` 인터페이스를 통해 **MFRC522 기반 RFID 모듈과 SPI 통신하고, MIFARE Classic 카드의 UID 및 메모리 데이터를 확인하는 C 프로그램**입니다.

이 프로젝트는 다음 두 가지 방식으로 사용할 수 있습니다.

1. `rfidctl`을 실행하여 대화형 터미널처럼 사용
2. `readUID`, `readBlock`, `readSector` 실행파일을 각각 직접 실행

| 프로그램 | 기능 |
|---|---|
| `rfidctl` | UID / Block / Sector 읽기 기능을 하나의 대화형 CLI에서 실행 |
| `readUID` | PICC의 UID 읽기 |
| `readBlock` | MIFARE Classic의 특정 Sector/Block 읽기 |
| `readSector` | MIFARE Classic의 특정 Sector 전체 읽기 |

<br>

## 목차

1. [사용 장치 및 환경](#1-사용-장치-및-환경)
    1.1 [Hardware](#11-hardware)
    1.2 [Build Environment](#12-build-environment)
    1.3 [Runtime Environment](#13-runtime-environment)
2. [프로젝트 파일 구성](#2-프로젝트-파일-구성)
3. [빌드](#3-빌드)
4. [프로그램 사용법](#4-프로그램-사용법)
    4.1 [내장 명령어 요약](#41-내장-명령어-요약)
    4.2 [UID 읽기](#42-uid-읽기)
    4.3 [Block 읽기](#43-block-읽기)
    4.4 [Sector 읽기](#44-sector-읽기)
    4.5 [도움말](#45-도움말)
    4.6 [종료](#46-종료)
5. [MIFARE Classic 1K 메모리 구조](#5-mifare-classic-1k-메모리-구조)
    5.1 [Sector Trailer 주의사항](#51-sector-trailer-주의사항)
    5.2 [5.2 기본 인증 Key (Key A)](#52-기본-인증-key-key-a)
6. [RFID 처리 흐름](#6-rfid-처리-흐름)
7. [빠른 시작](#7-빠른-시작)

<br>

## 1. 사용 장치 및 환경

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

빌드 흐름:

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

### 1.3 Runtime Environment

```text
Execution Board : Raspberry Pi 4 Model B
OS              : Raspberry Pi Linux
SPI Device      : /dev/spidev0.0
SPI Mode        : SPI Mode 0
Bits per word   : 8 bit
SPI Speed       : 1 MHz
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

현재 `include/spi_dev.h` 설정:

```c
#define SPI_DEVICE  "/dev/spidev0.0"
#define BIT_LEN     8U
#define SPEED_HZ    1000000U
#define MODE        SPI_MODE_0
```

사용하는 SPI controller, Chip Select 또는 Device Tree 설정이 다르면 환경에 맞게 수정합니다.

<br>

## 2. 프로젝트 파일 구성

최종 프로젝트 구조는 다음과 같습니다.

```text
linux-spi-rfid-seminar/
├── src/
│   ├── MFRC522.c
│   ├── spi_dev.c
│   ├── readUID.c
│   ├── readBlock.c
│   ├── readSector.c
│   └── rfidctl.c
│
├── include/
│   ├── MFRC522.h
│   ├── PICC.h
│   └── spi_dev.h
│
├── bin/
│   ├── readUID
│   ├── readBlock  
│   └── readSector
│
├── rfidctl
├── makefile
└── README.md
```

```text
src/      : C source
include/  : Header
bin/      : readUID / readBlock / readSector 실행파일
rfidctl   : 프로젝트 root에서 실행하는 대화형 CLI
```

`rfidctl`은 `bin/`의 개별 실행파일을 `fork()` + `execv()`로 실행합니다.

<br>

## 3. 빌드

프로젝트 디렉터리로 이동합니다.

```bash
cd <project-directory>
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

## 4. 프로그램 사용법

`rfidctl`은 프로젝트 root에 생성됩니다. 따라서 프로젝트 root에서 다음과 같이 실행합니다.

실행:

```bash
./rfidctl
```

실행 화면:

```text
RFID Control Terminal
Type 'help' for commands.
Type 'exit' or 'quit' to terminate.

rfidctl>
```

### 4.1 내장 명령어 요약

| 명령 | 설명 |
|---|---|
| `uid` | 카드의 UID를 읽습니다. (4byte or 7byte) |
| `readblock <sector> <block>` | 지정한 Sector의 Block 하나를 읽습니다. |
| `readsector <sector>` | 지정한 Sector의 Block 4개를 순서대로 읽습니다. |
| `help` | 사용할 수 있는 명령과 예제를 출력합니다. |
| `exit`, `quit` | 프로그램(rfidctl)을 종료합니다. |

cf. `rfidctl`을 사용하지 않고 `bin/`에 생성된 실행파일을 직접 사용할 수도 있습니다.

### 4.2 UID 읽기

```text
uid
```

출력 예시:

```text
rfidctl> uid
UID = 93 AC 51 56
```

### 4.3 Block 읽기

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

### 4.4 Sector 읽기

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

### 4.5 도움말

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

### 4.6 종료

```
exit | quit
```

종료하면 `rfidctl` 프로세스가 끝나고 shell prompt로 돌아갑니다.

<br>

## 5. MIFARE Classic 1K 메모리 구조

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

### 5.1 Sector Trailer 주의사항

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

### 5.2 기본 인증 Key (Key A)

현재 공통 MFRC522 코드에는 기본 Key A가 다음과 같이 정의되어 있습니다.

```text
FF FF FF FF FF FF
```

다른 Key를 사용하는 Sector를 읽으려면 `key_a` 값을 해당 카드 설정에 맞게 변경해야 합니다.

<br>

## 6. RFID 처리 흐름

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

<br>

## 7. 빠른 시작

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