# Project Name
TARGET = CHOMPI

DEBUG=0
# OPT=-O0
OPT=-O3

APP_TYPE=BOOT_SRAM
# make program-boot installs CHOMPI's own bootloader, not libDaisy's generic one
BOOT_BIN = ../../bin/CHOMPI_Bootloader_V6_2_0.bin
LDSCRIPT = chompi_sram.lds

LDSCRIPT=chompi_sram.lds

# FreeRTOS coreJSON
C_SOURCES = ../libs/coreJSON/source/core_json.c
C_INCLUDES +=  -I../libs/coreJSON/source/include

# Sources
CPP_SOURCES = chompi_main.cpp \
	encoder.cpp \
	FileStreamingManager.cpp

USE_FATFS = 1

# Libs
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
