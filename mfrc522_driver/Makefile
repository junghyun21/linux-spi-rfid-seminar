CC := aarch64-linux-gnu-gcc					# ARM64 Linux용 크로스 컴파일러

SRC_DIR := src
INC_DIR := include
BIN_DIR := bin

CFLAGS := -Wall -Wextra -I$(INC_DIR)		# 사용자 정의 헤더를 include/에서도 찾아라

# 전용 Device Driver와 통신할 때 사용하는 공통 헤더
COMMON_HDR := $(INC_DIR)/mfrc522_ioctl.h

# 최종적으로 만들어야 할 실행파일 목록
RFIDCTL := rfidctl_mfrc522_driver

CHILD_TARGETS := $(BIN_DIR)/readUID \
                 $(BIN_DIR)/readBlock \
                 $(BIN_DIR)/readSector

TARGETS := $(RFIDCTL) $(CHILD_TARGETS)

all: $(TARGETS)


# bin/ 디렉토리가 없으면 생성
$(BIN_DIR):
	mkdir -p $(BIN_DIR)


$(RFIDCTL): $(SRC_DIR)/rfidctl_mfrc522_driver.c
	$(CC) $(CFLAGS) $(SRC_DIR)/rfidctl_mfrc522_driver.c -o $@


# bin/ 디렉토리의 수정시간이 바뀌어도 다시 컴파일 x
$(BIN_DIR)/readUID: $(SRC_DIR)/readUID.c $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readUID.c \
		-o $@


$(BIN_DIR)/readBlock: $(SRC_DIR)/readBlock.c $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readBlock.c \
		-o $@


$(BIN_DIR)/readSector: $(SRC_DIR)/readSector.c $(COMMON_HDR) | $(BIN_DIR)
	$(CC) $(CFLAGS) \
		$(SRC_DIR)/readSector.c \
		-o $@


clean:
	rm -f $(RFIDCTL) $(CHILD_TARGETS)


# all, clean은 실제 파일명이 아니라 명령용 target
.PHONY: all clean