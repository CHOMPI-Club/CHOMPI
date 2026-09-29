# CHOMPI bootloader v6.4 — based on the Electrosmith Daisy Bootloader Makefile (v6.4).

PROJECT_NAME = chompi_bootloader

VERSION_MAJ = 6
VERSION_MIN = 4

# Boot window in milliseconds (upstream default: 2000).
C_DEFS += -DDSY_BOOT_TIMEOUT_MS=100

# USB descriptor strings shown while the bootloader is a DFU device.
C_DEFS += -DDSY_USB_DESC_MFR_STR=\"Chompi\ Club\"
C_DEFS += -DDSY_USB_DESC_PRODUCT_STR=\"CHOMPI\ Bootloader\"

# Uncomment to run DFU on the external (HS) USB port instead of the internal (FS) one.
# C_DEFS += -DDSY_DFU_USE_EXT_USB=1

EXTRA_C_DEFS ?=
TARGET_SUFFIX ?=

TARGET = $(PROJECT_NAME)_v$(VERSION_MAJ)_$(VERSION_MIN)${TARGET_SUFFIX}

CPP_SOURCES = main.cpp \
              $(wildcard ../shared/*.cpp)

USE_FATFS = 1

C_INCLUDES = -I. \
             -I../shared \

C_SOURCES += \
../cube_dfu/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Src/usbd_dfu.c \
../cube_dfu/USB_DEVICE/App/usbd_desc.c

C_INCLUDES += \
-I../cube_dfu/Middlewares/ST/STM32_USB_Device_Library/Class/DFU/Inc \
-I../cube_dfu/USB_DEVICE/App

C_INCLUDES += $(EXTRA_C_DEFS)

LDSCRIPT = boot_linker.lds

# WARNING -- an opt of -O3 causes the jump to fail and makes USB connectivity unreliable
OPT = -O0
DEBUG = 1

LIBDAISY_DIR = ../libs/libDaisy

SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
