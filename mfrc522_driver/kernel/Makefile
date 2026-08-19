obj-m += mfrc522_drv.o


# ============================================================
# Kernel build configuration
# ============================================================

ARCH := arm64

# 커널 모듈은 우리가 GCC를 직접 호출 x
# Linux Kernel의 Kbuild 시스템에게 빌드 맡김 -> Kbuild는 컴파일만 하는 게 아니라 여러 toolchain 프로그램을 사용
# 특정 프로그램 하나가 아니라 이 도구들의 공통 앞부분(prefix) 을 알려주는 변수
CROSS_COMPILE := aarch64-linux-gnu-    

# Raspberry Pi kernel build/source tree
# Linux Build Environment에 있는 Raspberry Pi용 커널 source/build tree 위치 -> 환경에 맞게 변경 필요
KDIR ?= $(HOME)/Raspberrypi_dev/linux

PWD := $(shell pwd)


# ============================================================
# Device Tree Overlay
# ============================================================

DTS  := mfrc522_overlay.dts
DTBO := mfrc522.dtbo


# ============================================================
# Targets
# ============================================================

.PHONY: all module overlay clean


all: module overlay


# ------------------------------------------------------------
# Kernel Module
# 1. -C KDIR($(HOME)/linux): 해당 디렉터리로 이동해서 make 실행
# 2. M=PWD : 외부 모듈 소스는 이 디렉터리에 존재 (/home/junghyun/mfrc522_driver 내 mfrc522.c를 찾아서 external modulef로 빌드)
#		   : obj-m += mfrc522_drv.o 로 설정했기 때문에 Kernel Build System은 mfrc522_drv.c를 골라서 빌드
# 3. modules : 지정한 M= 디렉터리에 있는 외부 Kernel Module을 빌드
# ------------------------------------------------------------

module:
	$(MAKE) -C $(KDIR) \
		M=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		modules


# ------------------------------------------------------------
# Device Tree Overlay
# 1. @ : 디바이스 트리 오버레이(.dtbo)를 합칠 수 있도록 심볼(__symbols__) 정보를 추가하는 옵션
# 2. -I dts : 입력 파일은 Device Tree Source 형식
# 3. -O dtb : 출력은 Device Tree Blob binary 형식
# 4. -o $(DTBO) : output filename
# 5. $(DTS) : 컴파일할 입력 DTS 파일
# ------------------------------------------------------------

overlay:
	dtc -@ \
		-I dts \
		-O dtb \
		-o $(DTBO) \
		$(DTS)


# ------------------------------------------------------------
# Clean
# ------------------------------------------------------------

clean:
	$(MAKE) -C $(KDIR) \
		M=$(PWD) \
		ARCH=$(ARCH) \
		CROSS_COMPILE=$(CROSS_COMPILE) \
		clean

	rm -f $(DTBO)