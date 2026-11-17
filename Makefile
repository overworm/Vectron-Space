OPENSNES := $(shell \
  if [ -f ../../../make/common.mk ]; then cd ../../.. && pwd; \
  elif [ -f ../../make/common.mk ]; then cd ../.. && pwd; \
  else echo "ERROR: cannot find OpenSNES root"; exit 1; fi)

TARGET      := space_fps.sfc
ROM_NAME    := SPACE RAYCAST FPS

CSRC        := main.c
ASMSRC      := gsu_loader.asm
GSUSRC      := gsu_fc.sfx gsu_w.sfx gsu_s.sfx

ROM_BANKS   := 64
ROM_REGION  := pal            # PAL (v13: the 224 px frame needs the PAL blank time)

GSU_RAM_KB  := 64

USE_SNESMOD := 1
SOUNDBANK_SRC := snd/music.it snd/boss.it snd/sfx.it
SOUNDBANK_BANK := 40
USE_LIB     := 1
USE_SUPERFX := 1
LIB_MODULES := console sprite dma background input

include $(OPENSNES)/make/common.mk
