#!/bin/bash
BINNAME=build/main.bin
dfu-util -a 0 -s 0x08000000:leave -D ./$BINNAME -d ,0483:df11
