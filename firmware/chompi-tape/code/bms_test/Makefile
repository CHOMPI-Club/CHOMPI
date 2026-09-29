# Project Name
TARGET = CHOMPI

DEBUG=1
OPT=-O0

APP_TYPE=BOOT_SRAM
LDSCRIPT=chompi_sram.lds

# Sources
CPP_SOURCES = chompi_main.cpp \

# USE_FATFS = 1

# Libs
LIBDAISY_DIR = ../libs/libDaisy
DAISYSP_DIR = ../libs/DaisySP

# Core location, and generic Makefile
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
