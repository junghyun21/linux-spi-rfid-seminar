CC := aarch64-linux-gnu-gcc					# ARM64 Linux용 크로스 컴파일러

SRC_DIR := src
INC_DIR := include
BIN_DIR := bin

CFLAGS := -Wall -Wextra -I$(INC_DIR)		# 사용자 정의 헤더를 include/에서도 찾아라

# 모든 RFID 프로그램이 공통으로 사용하는 구현 파일
COMMON_SRC := $(SRC_DIR)/MFRC522.c \
              $(SRC_DIR)/spi_dev.c

# 공통 헤더
COMMON_HDR := $(INC_DIR)/MFRC522.h \
              $(INC_DIR)/PICC.h \
              $(INC_DIR)/spi_dev.h

# 최종적으로 만들어야 할 실행파일 목록
RFIDCTL := rfidctl

CHILD_TARGETS := $(BIN_DIR)/readUID \
                 $(BIN_DIR)/readBlock \
                 $(BIN_DIR)/readSector

TARGETS := $(RFIDCTL) $(CHILD_TARGETS)

all: $(TARGETS)


# bin/ 디렉토리가 없으면 생성
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(RFIDCTL): $(SRC_DIR)/rfidctl.c
	$(CC) $(CFLAGS) $(SRC_DIR)/rfidctl.c -o $@

# bin/ 디렉토리의 수정시간이 바뀌어도 다시 컴파일 x
$(BIN_DIR)/readUID: $(SRC_DIR)/readUID.c $(COMMON_SRC) $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readUID.c \
		$(COMMON_SRC) \
		-o $@

$(BIN_DIR)/readBlock: $(SRC_DIR)/readBlock.c $(COMMON_SRC) $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readBlock.c \
		$(COMMON_SRC) \
		-o $@

$(BIN_DIR)/readSector: $(SRC_DIR)/readSector.c $(COMMON_SRC) $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readSector.c \
		$(COMMON_SRC) \
		-o $@


clean:
	rm -f $(RFIDCTL) $(CHILD_TARGETS)


# all, clean은 실제 파일명이 아니라 명령용 target
.PHONY: all clean